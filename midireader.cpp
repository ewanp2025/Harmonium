#include "midireader.h"
#include <vector>

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

#ifdef _WIN32
static void CALLBACK winMidiInProc(HMIDIIN, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR) {
    if (wMsg != MIM_DATA) return;
    auto* cb = reinterpret_cast<MidiReader::NoteCallback*>(dwInstance);
    if (!cb || !(*cb)) return;

    unsigned char status = dwParam1 & 0xFF;
    unsigned char data1 = (dwParam1 >> 8) & 0x7F;
    unsigned char data2 = (dwParam1 >> 16) & 0x7F;
    unsigned char cmd = status & 0xF0;

    if (cmd == 0x90 && data2 > 0) {
        (*cb)(data1, data2, true);
    } else if (cmd == 0x80 || (cmd == 0x90 && data2 == 0)) {
        (*cb)(data1, 0, false);
    }
}
#endif

void MidiReader::run() {
#ifdef __linux__
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        emit midiStatusChanged("ALSA MIDI Unavailable");
        return;
    }
    snd_seq_set_client_name(seq, "HarmoniumBox");
    int inPort = snd_seq_create_simple_port(
        seq, "MIDI In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<struct pollfd> pfds(npfd);
    snd_seq_poll_descriptors(seq, pfds.data(), npfd, POLLIN);

    int myClient = snd_seq_client_id(seq);
    int scanCounter = 0;

    while (m_running) {
        if (scanCounter++ % 30 == 0) {
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
                    if ((cap & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) ==
                        (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) {
                        int port = snd_seq_port_info_get_port(pinfo);
                        snd_seq_connect_from(seq, inPort, client, port);
                        connectedName = QString::fromUtf8(name);
                    }
                }
            }
            emit midiStatusChanged(connectedName);
        }

        if (poll(pfds.data(), npfd, 50) > 0) {
            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
                if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0) {
                    if (m_callback) m_callback(ev->data.note.note, ev->data.note.velocity, true);
                } else if (ev->type == SND_SEQ_EVENT_NOTEOFF ||
                           (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0)) {
                    if (m_callback) m_callback(ev->data.note.note, 0, false);
                }
                snd_seq_free_event(ev);
            }
        }
    }
    snd_seq_close(seq);

#elif defined(_WIN32)
    HMIDIIN hMidiDevice = nullptr;
    UINT openedDevId = 0xFFFFFFFF;

    while (m_running) {
        UINT numDevs = midiInGetNumDevs();
        if (numDevs == 0) {
            if (hMidiDevice) {
                midiInStop(hMidiDevice);
                midiInClose(hMidiDevice);
                hMidiDevice = nullptr;
                openedDevId = 0xFFFFFFFF;
            }
            emit midiStatusChanged("PC Mode: Touch/PC Keys Active (Plug USB-MIDI any time)");
        } else if (!hMidiDevice) {
            MIDIINCAPS caps;
            if (midiInGetDevCaps(0, &caps, sizeof(MIDIINCAPS)) == MMSYSERR_NOERROR) {
                if (midiInOpen(&hMidiDevice, 0, reinterpret_cast<DWORD_PTR>(winMidiInProc),
                               reinterpret_cast<DWORD_PTR>(&m_callback), CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                    midiInStart(hMidiDevice);
                    openedDevId = 0;
                    emit midiStatusChanged(QString("MIDI Connected: %1").arg(QString::fromWCharArray(caps.szPname)));
                }
            }
        }
        msleep(500);
    }

    if (hMidiDevice) {
        midiInStop(hMidiDevice);
        midiInClose(hMidiDevice);
    }
#else
    emit midiStatusChanged("On-Screen Keyboard Mode");
#endif
}
