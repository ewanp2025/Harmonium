#ifndef MIDIREADER_H
#define MIDIREADER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <array>
#include <cstdint>
#include <functional>

class MidiReader : public QThread {
    Q_OBJECT
public:
    // channel is 0..15 (corresponding to MIDI Channels 1..16)
    using NoteCallback = std::function<void(int channel, int midiNote, int velocity, bool noteOn)>;

    explicit MidiReader(NoteCallback cb, QObject* parent = nullptr);
    ~MidiReader() override;

    void stopReader();

    // Real-time safe lock-free MIDI Output queue methods
    void sendNoteOn(int channel, int midiNote, int velocity);
    void sendNoteOff(int channel, int midiNote);
    void sendAllNotesOff();

signals:
    void midiStatusChanged(const QString& deviceName);

protected:
    void run() override;

private:
    struct MidiOutEvent {
        uint8_t status = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
    };

    void enqueueEvent(uint8_t status, uint8_t data1, uint8_t data2);
    bool dequeueEvent(MidiOutEvent& ev);

    NoteCallback m_callback;
    std::atomic<bool> m_running{true};

    static constexpr size_t RING_SIZE = 512;
    std::array<MidiOutEvent, RING_SIZE> m_outRing{};
    std::atomic<size_t> m_writeIdx{0};
    std::atomic<size_t> m_readIdx{0};
};

#endif // MIDIREADER_H