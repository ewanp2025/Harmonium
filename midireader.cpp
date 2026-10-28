#include "midireader.h"
#include <vector>
#include <algorithm>

#ifdef __linux__
#include <alsa/asoundlib.h>
#include <poll.h>
#elif defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

MidiReader::MidiReader(NoteCallback cb, QObject* parent)
    : QThread(parent), m_callback(std::move(cb)) {}

MidiReader::~MidiReader() {
    stopReader();
    wait();
}

void MidiReader::stopReader() {
    m_running = false;
}

void MidiReader::enqueueEvent(uint8_t status, uint8_t data1, uint8_t data2) {
    size_t w = m_writeIdx.load(std::memory_order_relaxed);
    size_t nextW = (w + 1) % RING_SIZE;
    if (nextW == m_readIdx.load(std::memory_order_acquire)) {
        return; // Ring full, drop without blocking audio thread
    }
    m_outRing[w] = {status, data1, data2};
    m_writeIdx.store(nextW, std::memory_order_release);
}

bool MidiReader::dequeueEvent(MidiOutEvent& ev) {
    size_t r = m_readIdx.load(std::memory_order_relaxed);
    if (r == m_writeIdx.load(std::memory_order_acquire)) {
        return false;
    }
    ev = m_outRing[r];
    m_readIdx.store((r + 1) % RING_SIZE, std::memory_order_release);
    return true;
}

void MidiReader::sendNoteOn(int channel, int midiNote, int velocity) {
    int ch = std::clamp(channel, 0, 15);
    int note = std::clamp(midiNote, 0, 127);
    int vel = std::clamp(velocity, 1, 127);
    enqueueEvent(static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), static_cast<uint8_t>(vel));
}

void MidiReader::sendNoteOff(int channel, int midiNote) {
    int ch = std::clamp(channel, 0, 15);
    int note = std::clamp(midiNote, 0, 127);
    enqueueEvent(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 0);
}

void MidiReader::sendAllNotesOff() {
    for (int ch = 0; ch < 16; ++ch) {
        enqueueEvent(static_cast<uint8_t>(0xB0 | ch), 123, 0); // CC 123: All Notes Off
    }
}

#ifdef _WIN32
static void CALLBACK winMidiInProc(HMIDIIN, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR) {
    if (wMsg != MIM_DATA) return;
    auto* cb = reinterpret_cast<MidiReader::NoteCallback*>(dwInstance);
    if (!cb || !(*cb)) return;

    unsigned char status = dwParam1 & 0xFF;
    unsigned char data1 = (dwParam1 >> 8) & 0x7F;
    unsigned char data2 = (dwParam1 >> 16) & 0x7F;
    unsigned char cmd = status & 0xF0;
    int channel = status & 0x0F;

    if (cmd == 0x90 && data2 > 0) {
        (*cb)(channel, data1, data2, true);
    } else if (cmd == 0x80 || (cmd == 0x90 && data2 == 0)) {
        (*cb)(channel, data1, 0, false);
    }
}
#endif

void MidiReader::run() {
#ifdef __linux__
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        emit midiStatusChanged("ALSA MIDI Unavailable");
        return;
    }
    snd_seq_set_client_name(seq, "HarmoniumBox");
    int inPort = snd_seq_create_simple_port(
        seq, "MIDI In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    int outPort = snd_seq_create_simple_port(
        seq, "MIDI Out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<struct pollfd> pfds(npfd);
    snd_seq_poll_descriptors(seq, pfds.data(), npfd, POLLIN);

    int myClient = snd_seq_client_id(seq);
    int scanCounter = 0;

    while (m_running) {
        // Periodically scan & auto-connect ALSA MIDI In and Out ports
        if (scanCounter++ % 200 == 0) {
            snd_seq_client_info_t* cinfo;
            snd_seq_port_info_t* pinfo;
            snd_seq_client_info_alloca(&cinfo);
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_client_info_set_client(cinfo, -1);
            QString connectedName = "No USB Keyboard (Touch Keys Active)";

            while (snd_seq_query_next_client(seq, cinfo) >= 0) {
                int client = snd_seq_client_info_get_client(cinfo);
                if (client == 0 || client == myClient) continue;
                const char* name = snd_seq_client_info_get_name(cinfo);
                if (QString(name).contains("Through", Qt::CaseInsensitive)) continue;

                snd_seq_port_info_set_client(pinfo, client);
                snd_seq_port_info_set_port(pinfo, -1);
                while (snd_seq_query_next_port(seq, pinfo) >= 0) {
                    unsigned int cap = snd_seq_port_info_get_capability(pinfo);
                    int port = snd_seq_port_info_get_port(pinfo);
                    if ((cap & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) ==
                        (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) {
                        snd_seq_connect_from(seq, inPort, client, port);
                        connectedName = QString("MIDI Connected: %1").arg(QString::fromUtf8(name));
                    }
                    if ((cap & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) ==
                        (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                        snd_seq_connect_to(seq, outPort, client, port);
                    }
                }
            }
            emit midiStatusChanged(connectedName);
        }

        // Flush outgoing MIDI events
        MidiOutEvent outEv;
        while (dequeueEvent(outEv)) {
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            snd_seq_ev_set_source(&ev, outPort);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);

            uint8_t cmd = outEv.status & 0xF0;
            uint8_t ch = outEv.status & 0x0F;
            if (cmd == 0x90) {
                snd_seq_ev_set_noteon(&ev, ch, outEv.data1, outEv.data2);
            } else if (cmd == 0x80) {
                snd_seq_ev_set_noteoff(&ev, ch, outEv.data1, 0);
            } else if (cmd == 0xB0) {
                snd_seq_ev_set_controller(&ev, ch, outEv.data1, outEv.data2);
            }
            snd_seq_event_output_direct(seq, &ev);
        }

        if (poll(pfds.data(), npfd, 5) > 0) {
            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
                if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0) {
                    if (m_callback) m_callback(ev->data.note.channel, ev->data.note.note, ev->data.note.velocity, true);
                } else if (ev->type == SND_SEQ_EVENT_NOTEOFF ||
                           (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0)) {
                    if (m_callback) m_callback(ev->data.note.channel, ev->data.note.note, 0, false);
                }
                snd_seq_free_event(ev);
            }
        }
    }
    snd_seq_close(seq);

#elif defined(_WIN32)
    HMIDIIN hMidiIn = nullptr;
    HMIDIOUT hMidiOut = nullptr;
    int scanCounter = 0;

    while (m_running) {
        if (scanCounter++ % 100 == 0) {
            UINT numInDevs = midiInGetNumDevs();
            if (numInDevs == 0) {
                if (hMidiIn) {
                    midiInStop(hMidiIn);
                    midiInClose(hMidiIn);
                    hMidiIn = nullptr;
                }
                emit midiStatusChanged("PC Mode: Touch/PC Keys Active (Plug USB-MIDI any time)");
            } else if (!hMidiIn) {
                MIDIINCAPS caps;
                if (midiInGetDevCaps(0, &caps, sizeof(MIDIINCAPS)) == MMSYSERR_NOERROR) {
                    if (midiInOpen(&hMidiIn, 0, reinterpret_cast<DWORD_PTR>(winMidiInProc),
                                   reinterpret_cast<DWORD_PTR>(&m_callback), CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                        midiInStart(hMidiIn);
                        emit midiStatusChanged(QString("MIDI Connected: %1").arg(QString::fromWCharArray(caps.szPname)));
                    }
                }
            }

            UINT numOutDevs = midiOutGetNumDevs();
            if (numOutDevs == 0) {
                if (hMidiOut) {
                    midiOutClose(hMidiOut);
                    hMidiOut = nullptr;
                }
            } else if (!hMidiOut) {
                // Prefer external USB/Hardware MIDI Out port over Microsoft GS Wavetable Synth if available
                UINT targetOutId = (numOutDevs > 1) ? 1 : 0;
                midiOutOpen(&hMidiOut, targetOutId, 0, 0, CALLBACK_NULL);
            }
        }

        MidiOutEvent outEv;
        while (dequeueEvent(outEv)) {
            if (hMidiOut) {
                DWORD msg = static_cast<DWORD>(outEv.status) |
                            (static_cast<DWORD>(outEv.data1) << 8) |
                            (static_cast<DWORD>(outEv.data2) << 16);
                midiOutShortMsg(hMidiOut, msg);
            }
        }

        msleep(4);
    }

    if (hMidiIn) {
        midiInStop(hMidiIn);
        midiInClose(hMidiIn);
    }
    if (hMidiOut) {
        midiOutReset(hMidiOut);
        midiOutClose(hMidiOut);
    }
#else
    emit midiStatusChanged("On-Screen Keyboard Mode");
#endif
}