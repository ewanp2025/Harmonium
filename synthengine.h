#ifndef SYNTHENGINE_H
#define SYNTHENGINE_H

#include <QIODevice>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QMutex>
#include <atomic>
#include <vector>
#include <array>
#include <QElapsedTimer>
#include "effect.h"
#include "projectio.h"
#include "xpressive_parser.h"
#include "midireader.h"

class SynthEngine : public QIODevice {
    Q_OBJECT

public:
    explicit SynthEngine(QObject *parent = nullptr);
    ~SynthEngine() override;

    static QString defaultHarmoniumFormula();

    void setMidiInterface(MidiReader* midi) { m_midi = midi; }

    void loadSong(const MmpSong& song);
    MmpSong getSongSnapshot();

    void selectTala(int talaIndex);
    int getSelectedTala() const { return m_currentTalaIdx.load(); }

    void selectHarmoniumSong(int songIndex);
    int getSelectedHarmoniumSong() const { return m_currentHarmSongIdx.load(); }

    void updateTalaTheka(int talaIdx, const TalaTheka& updatedTala);
    void addTalaTheka(const TalaTheka& newTala);
    void updateHarmoniumSong(int songIdx, const HarmoniumSong& updatedSong);
    void addHarmoniumSong(const HarmoniumSong& newSong);

    // Randomisers (updates active slot in memory and compiles immediately)
    HarmoniumSong randomiseCurrentRagaSong();
    TalaTheka randomiseCurrentTalaTheka();

    void auditionTrack(int trackIdx);
    void auditionBolByName(const QString& bolText);
    void auditionSwara(const QString& swaraText);

    int getCurrentDrumTick() const { return m_currentDrumTick.load(); }
    int getCurrentSongTick() const { return m_currentSongTick.load(); }

    void startWithCountdown(bool enableCountdown);
    void stopPlayback();
    void togglePause();

    bool isPlaying() const { return m_isPlaying.load(); }
    bool isPaused() const { return m_isPaused.load(); }
    int countdownBeatsLeft() const { return m_countdownBeats.load(); }

    void setBpm(int bpm);
    int getBpm() const { return m_bpm.load(); }

    void setTrackMuted(int trackIdx, bool muted);
    bool isTrackMuted(int trackIdx) const;

    void setHarmoniumPresetMode(bool presetAutoOn);
    bool isHarmoniumPresetMode() const { return m_harmoniumPresetMode.load(); }
    bool setHarmoniumFormula(const QString& expr);

    void setDroneEnabled(bool enabled);
    bool isDroneEnabled() const { return m_droneEnabled.load(); }
    void setDronePreset(int presetIdx) { m_dronePreset.store(std::clamp(presetIdx, 0, 2)); }
    int getDronePreset() const { return m_dronePreset.load(); }
    void setDroneSwaraMode(int modeIdx) { m_droneSwaraMode.store(std::clamp(modeIdx, 0, 3)); }
    int getDroneSwaraMode() const { return m_droneSwaraMode.load(); }

    void setOctaveShift(int octaves) { m_octaveShift.store(std::clamp(octaves, -3, 3)); }
    int getOctaveShift() const { return m_octaveShift.load(); }

    void setTuning(int saSemitone, double a4Hz);
    int getSaSemitone() const { return m_saSemitone.load(); }
    double getA4Hz() const { return m_a4Hz.load(); }

    void setRagaTrackSource(int sourceIdx) { m_ragaSourceTrack.store(sourceIdx); }
    int getCurrentDetectedMidiNote() const { return m_detectedMidiNote.load(); }



    // MIDI In / Out Configuration
    void setMidiInEnabled(bool en) { m_midiInEnabled.store(en); }
    bool isMidiInEnabled() const { return m_midiInEnabled.load(); }
    // In Mode: 0 = Dual Ch (Harm + Drums), 1 = Split (<52 Drum, >=52 Harm), 2 = Harm Only, 3 = Drums Only
    void setMidiInMode(int mode) { m_midiInMode.store(std::clamp(mode, 0, 3)); }
    int getMidiInMode() const { return m_midiInMode.load(); }
    // Channels: -1 = Any, 0..15 = MIDI Ch 1..16
    void setMidiHarmChannel(int ch) { m_midiHarmChannel.store( std::clamp(ch, -1, 15)); }
    int getMidiHarmChannel() const { return m_midiHarmChannel.load(); }
    void setMidiDrumChannel(int ch) { m_midiDrumChannel.store(std::clamp(ch, -1, 15)); }
    int getMidiDrumChannel() const { return m_midiDrumChannel.load(); }

    void setMidiOutEnabled(bool en);
    bool isMidiOutEnabled() const { return m_midiOutEnabled.load(); }
    // Out Mode: 0 = Harm + Drums, 1 = Harm Only, 2 = Drums Only
    void setMidiOutMode(int mode) { m_midiOutMode.store(std::clamp(mode, 0, 2)); }
    int getMidiOutMode() const { return m_midiOutMode.load(); }

    // Maps MIDI Note to Drum Track Index (0..15) and vice versa (GM 36..51)
    static int midiNoteToDrumTrack(int midiNote);
    static int drumTrackToMidiNote(int trackIdx) { return 36 + std::clamp(trackIdx, 0, 15); }

    void handleIncomingMidi(int channel, int midiNote, int velocity, bool isNoteOn);
    void triggerDrumTrackFromMidi(int trackIdx, int velocity);

    void noteOn(int midiNote, int velocity, bool fromExternalMidi = false);
    void noteOff(int midiNote, bool fromExternalMidi = false);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    float getDspLoad() const { return m_dspLoad.load(std::memory_order_relaxed); }

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    struct RuntimeTrackVoice {
        bool active = false;
        int key = 69;
        float vel = 1.0f;
        double noteTime = 0.0;
        double noteDurSec = 0.25;
        bool released = false;
        double relTime = 0.0;
        double lastOut1 = 0.0;
        double lastOut2 = 0.0;
        int midiOutNote = -1;
        int midiOutChan = 0;
        std::vector<double> intState1;
        std::vector<double> intState2;
    };

    struct RuntimeTrack {
        MmpTrack spec;
        std::vector<BiquadFilter> filters;
        TransferFunctionFX tfFx;
        std::array<RuntimeTrackVoice, 6> voices;
        int nextVoice = 0;
    };

    struct HarmoniumVoice {
        bool active = false;
        bool held = false;
        int midiNote = 60;
        float velocity = 1.0f;
        double t = 0.0;
        double trel = 0.0;
        double lastOut = 0.0;
        std::array<double, 16> intState{};
    };

    struct DroneNoteState {
        double t = 0.0;
        std::array<double, 12> shrutiInt1{};
        std::array<double, 12> shrutiInt2{};
        std::array<double, 12> tanpuraInt1{};
        std::array<double, 12> tanpuraInt2{};
    };

    double midiToFreq(int midiNote) const;
    void triggerTrackVoice(RuntimeTrack& rt, int trackIdx, const SongNote& sn, double secPerTick);

    QAudioSink *m_audioSink = nullptr;
    QAudioFormat m_format;
    mutable QMutex m_mutex;
    MidiReader *m_midi = nullptr;

    double m_sampleRate = 22050.0;
    MmpSong m_song;
    std::vector<RuntimeTrack> m_runtimeTracks;

    std::shared_ptr<XpressiveParser::Node> m_harmoniumAst;
    std::array<HarmoniumVoice, 8> m_harmVoices;

    TransferFunctionFX m_liveHarmoniumTf;

    std::shared_ptr<XpressiveParser::Node> m_shrutiO1, m_shrutiO2;
    std::shared_ptr<XpressiveParser::Node> m_tanpuraO1, m_tanpuraO2;
    std::array<DroneNoteState, 3> m_droneNotes;



    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_harmoniumPresetMode{true};
    std::atomic<bool> m_droneEnabled{false};
    std::atomic<int> m_dronePreset{2};
    std::atomic<int> m_droneSwaraMode{0};
    std::atomic<bool> m_eqEnabled{true};
    std::atomic<bool> m_reverbEnabled{true};

    std::atomic<bool> m_midiInEnabled{true};
    std::atomic<int> m_midiInMode{0};       // 0 = Dual Ch, 1 = Split, 2 = Harm Only, 3 = Drums Only
    std::atomic<int> m_midiHarmChannel{-1}; // -1 = Any (Out defaults to Ch 1 / idx 0)
    std::atomic<int> m_midiDrumChannel{9};  // 9 = MIDI Channel 10

    std::atomic<bool> m_midiOutEnabled{false};
    std::atomic<int> m_midiOutMode{0};      // 0 = Harm+Drums, 1 = Harm Only, 2 = Drums Only

    std::atomic<int> m_currentTalaIdx{0};
    std::atomic<int> m_currentHarmSongIdx{0};
    std::atomic<int> m_drumLoopTicks{384};
    std::atomic<int> m_currentDrumTick{0};
    std::atomic<int> m_currentSongTick{0};
    std::atomic<int> m_bpm{140};
    std::atomic<int> m_octaveShift{0};
    std::atomic<int> m_saSemitone{0};
    std::atomic<double> m_a4Hz{440.0};

    std::atomic<int> m_countdownBeats{0};
    double m_countdownTimerSec = 0.0;
    double m_clickEnv = 0.0;

    double m_songTickPos = 0.0;
    int m_lastTriggeredTick = -1;

    std::atomic<int> m_ragaSourceTrack{-1};
    std::atomic<int> m_detectedMidiNote{60};

    std::atomic<float> m_dspLoad{0.0f};
};

#endif // SYNTHENGINE_H