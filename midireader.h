#ifndef MIDIREADER_H
#define MIDIREADER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

class MidiReader : public QThread {
    Q_OBJECT
public:
    using NoteCallback = std::function<void(int midiNote, int velocity, bool noteOn)>;

    explicit MidiReader(NoteCallback cb, QObject* parent = nullptr);
    ~MidiReader() override;

    void stopReader();

signals:
    void midiStatusChanged(const QString& deviceName);

protected:
    void run() override;

private:
    NoteCallback m_callback;
    std::atomic<bool> m_running{true};
};

#endif // MIDIREADER_H
