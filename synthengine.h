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
#include "effect.h"
#include "projectio.h"
#include "xpressive_parser.h"

class SynthEngine : public QIODevice {
    Q_OBJECT

public:
    explicit SynthEngine(QObject *parent = nullptr);
    ~SynthEngine() override;

    static QString defaultHarmoniumFormula();

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

    void setEqEnabled(bool en);
    void setReverbEnabled(bool en);
    bool isEqEnabled() const { return m_eqEnabled.load(); }
    bool isReverbEnabled() const { return m_reverbEnabled.load(); }

    void noteOn(int midiNote, int velocity);
    void noteOff(int midiNote);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

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
        std::vector<double> intState1;
        std::vector<double> intState2;
    };

    struct RuntimeTrack {
        MmpTrack spec;
        std::vector<BiquadFilter> filters;
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
    void triggerTrackVoice(RuntimeTrack& rt, const SongNote& sn, double secPerTick);

    QAudioSink *m_audioSink = nullptr;
    QAudioFormat m_format;
    mutable QMutex m_mutex;

    double m_sampleRate = 22050.0;
    MmpSong m_song;
    std::vector<RuntimeTrack> m_runtimeTracks;

    std::shared_ptr<XpressiveParser::Node> m_harmoniumAst;
    std::array<HarmoniumVoice, 8> m_harmVoices;

    std::shared_ptr<XpressiveParser::Node> m_shrutiO1, m_shrutiO2;
    std::shared_ptr<XpressiveParser::Node> m_tanpuraO1, m_tanpuraO2;
    std::array<DroneNoteState, 3> m_droneNotes;

    EightBandEQ m_eq;
    Plate2x2Reverb m_reverb;

    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_harmoniumPresetMode{true};
    std::atomic<bool> m_droneEnabled{false};
    std::atomic<int> m_dronePreset{2};
    std::atomic<int> m_droneSwaraMode{0};
    std::atomic<bool> m_eqEnabled{true};
    std::atomic<bool> m_reverbEnabled{true};

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
};

#endif // SYNTHENGINE_H
