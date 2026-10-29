#include "synthengine.h"
#include "drum_presets.h"
#include <QMutexLocker>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

QString SynthEngine::defaultHarmoniumFormula() {
    return BuiltInPresets::defaultHarmoniumO1();
}

int SynthEngine::midiNoteToDrumTrack(int midiNote) {
    // Direct GM mapping: 36..51 (C2..D#3) -> Tracks 0..15
    if (midiNote >= 36 && midiNote < 52) return midiNote - 36;
    // Middle C convenience mapping: 60..75 (C4..D#5) -> Tracks 0..15
    if (midiNote >= 60 && midiNote < 76) return midiNote - 60;
    // Wrap any other octave cleanly across the 16 percussion/shankha tracks
    return ((midiNote - 36) % 16 + 16) % 16;
}

SynthEngine::SynthEngine(QObject *parent)
    : QIODevice(parent) {

    m_format.setSampleRate(22050);
    m_format.setChannelCount(2);
    m_format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!device.isFormatSupported(m_format)) {
        m_format = device.preferredFormat();
    }
    m_sampleRate = std::max(8000.0, static_cast<double>(m_format.sampleRate()));

    m_liveHarmoniumTf.setSampleRate(m_sampleRate);
    m_liveHarmoniumTf.setFormula("((1 + 2.5 * exp(-(((freq-320)/100)^2)) + 1.2 * exp(-(((freq-1300)/200)^2))) * (1 + 0.35 * exp(-j * 2 * pi * freq * 0.002))) / sqrt(1 + (freq/7200)^8)");

    try { m_shrutiO1 = std::shared_ptr<XpressiveParser::Node>(XpressiveParser::parse(BuiltInPresets::shrutiDroneO1().toStdString()).release()); } catch (...) {}
    try { m_shrutiO2 = std::shared_ptr<XpressiveParser::Node>(XpressiveParser::parse(BuiltInPresets::shrutiDroneO2().toStdString()).release()); } catch (...) {}
    try { m_tanpuraO1 = std::shared_ptr<XpressiveParser::Node>(XpressiveParser::parse(BuiltInPresets::tanpuraDroneO1().toStdString()).release()); } catch (...) {}
    try { m_tanpuraO2 = std::shared_ptr<XpressiveParser::Node>(XpressiveParser::parse(BuiltInPresets::tanpuraDroneO2().toStdString()).release()); } catch (...) {}

    setHarmoniumFormula(defaultHarmoniumFormula());
    loadSong(ProjectIO::createBuiltInDefaultSong());

    m_audioSink = new QAudioSink(device, m_format, this);
    m_audioSink->setBufferSize(8192);
    open(QIODevice::ReadOnly);
    m_audioSink->start(this);
}

SynthEngine::~SynthEngine() {
    if (m_audioSink) {
        m_audioSink->stop();
    }
    close();
}

void SynthEngine::setMidiOutEnabled(bool en) {
    m_midiOutEnabled.store(en);
    if (!en && m_midi) {
        m_midi->sendAllNotesOff();
    }
}

void SynthEngine::setDroneEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);
    if (enabled && !m_droneEnabled.load()) {
        for (int i = 0; i < 3; ++i) {
            m_droneNotes[i].t = i * 0.25;
            m_droneNotes[i].shrutiInt1.fill(0.0);
            m_droneNotes[i].shrutiInt2.fill(0.0);
            m_droneNotes[i].tanpuraInt1.fill(0.0);
            m_droneNotes[i].tanpuraInt2.fill(0.0);
        }
    }
    m_droneEnabled.store(enabled);
}

bool SynthEngine::setHarmoniumFormula(const QString& expr) {
    try {
        int count = 0;
        auto parsed = XpressiveParser::parse(expr.toStdString(), &count);
        QMutexLocker locker(&m_mutex);
        m_harmoniumAst = std::shared_ptr<XpressiveParser::Node>(parsed.release());
        return true;
    } catch (...) {
        return false;
    }
}

void SynthEngine::loadSong(const MmpSong& song) {
    QMutexLocker locker(&m_mutex);
    m_song = song;
    m_bpm.store(song.bpm);
    m_saSemitone.store(song.saSemitone);
    m_a4Hz.store(song.a4Hz);
    m_currentTalaIdx.store(song.currentTalaIdx);
    m_currentHarmSongIdx.store(song.currentHarmSongIdx);
    m_drumLoopTicks.store(std::max(48, song.drumLoopTicks));

    m_runtimeTracks.clear();
    m_runtimeTracks.resize(song.tracks.size());

    for (size_t i = 0; i < song.tracks.size(); ++i) {
        m_runtimeTracks[i].spec = song.tracks[i];
        m_runtimeTracks[i].tfFx.setSampleRate(m_sampleRate);
        m_runtimeTracks[i].tfFx.setFormula(song.tracks[i].tfStr);
        ProjectIO::compileTrackAsts(m_runtimeTracks[i].spec);

        for (const auto& fSpec : m_runtimeTracks[i].spec.fxFilters) {
            BiquadFilter bf;
            if (fSpec.isHighpass) bf.configureHighpass(fSpec.cutoffHz, 0.707, m_sampleRate);
            else bf.configureLowpass(fSpec.cutoffHz, 0.707, m_sampleRate);
            m_runtimeTracks[i].filters.push_back(bf);
        }

        for (auto& v : m_runtimeTracks[i].voices) {
            v.active = false;
            v.midiOutNote = -1;
            v.intState1.assign(std::max(16, m_runtimeTracks[i].spec.numIntegrators), 0.0);
            v.intState2.assign(std::max(16, m_runtimeTracks[i].spec.numIntegrators), 0.0);
        }
    }
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
}

void SynthEngine::selectTala(int talaIndex) {
    QMutexLocker locker(&m_mutex);
    if (talaIndex < 0 || talaIndex >= static_cast<int>(m_song.talas.size())) return;

    m_currentTalaIdx.store(talaIndex);
    m_song.currentTalaIdx = talaIndex;
    const auto& tala = m_song.talas[talaIndex];
    m_song.totalLoopTicks = tala.loopTicks;
    m_song.drumLoopTicks = std::max(48, tala.drumLoopTicks);
    m_drumLoopTicks.store(m_song.drumLoopTicks);

    for (size_t t = 0; t < m_runtimeTracks.size() && t < tala.trackNotes.size(); ++t) {
        m_runtimeTracks[t].spec.notes = tala.trackNotes[t];
        for (auto& v : m_runtimeTracks[t].voices) v.active = false;
    }
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
}

void SynthEngine::selectHarmoniumSong(int songIndex) {
    QMutexLocker locker(&m_mutex);
    if (songIndex < 0 || songIndex >= static_cast<int>(m_song.harmoniumSongs.size())) return;
    m_currentHarmSongIdx.store(songIndex);
    m_song.currentHarmSongIdx = songIndex;
    ProjectIO::rebuildCompiledPatterns(m_song);

    int tIdx = m_currentTalaIdx.load();
    if (tIdx >= 0 && tIdx < static_cast<int>(m_song.talas.size())) {
        m_song.totalLoopTicks = m_song.talas[tIdx].loopTicks;
        for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[tIdx].trackNotes.size(); ++t) {
            m_runtimeTracks[t].spec.notes = m_song.talas[tIdx].trackNotes[t];
        }
    }
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
}

void SynthEngine::updateTalaTheka(int talaIdx, const TalaTheka& updatedTala) {
    QMutexLocker locker(&m_mutex);
    if (talaIdx < 0 || talaIdx >= static_cast<int>(m_song.talaThekas.size())) return;
    m_song.talaThekas[talaIdx] = updatedTala;
    ProjectIO::rebuildCompiledPatterns(m_song);
    m_drumLoopTicks.store(m_song.drumLoopTicks);
    int curTala = m_currentTalaIdx.load();
    if (curTala >= 0 && curTala < static_cast<int>(m_song.talas.size())) {
        for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[curTala].trackNotes.size(); ++t) {
            m_runtimeTracks[t].spec.notes = m_song.talas[curTala].trackNotes[t];
        }
    }
}

void SynthEngine::addTalaTheka(const TalaTheka& newTala) {
    QMutexLocker locker(&m_mutex);
    m_song.talaThekas.push_back(newTala);
    m_song.currentTalaIdx = static_cast<int>(m_song.talaThekas.size()) - 1;
    m_currentTalaIdx.store(m_song.currentTalaIdx);
    ProjectIO::rebuildCompiledPatterns(m_song);
    m_drumLoopTicks.store(m_song.drumLoopTicks);
    int curTala = m_currentTalaIdx.load();
    for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[curTala].trackNotes.size(); ++t) {
        m_runtimeTracks[t].spec.notes = m_song.talas[curTala].trackNotes[t];
    }
}

void SynthEngine::updateHarmoniumSong(int songIdx, const HarmoniumSong& updatedSong) {
    QMutexLocker locker(&m_mutex);
    if (songIdx < 0 || songIdx >= static_cast<int>(m_song.harmoniumSongs.size())) return;
    m_song.harmoniumSongs[songIdx] = updatedSong;
    ProjectIO::rebuildCompiledPatterns(m_song);
    int curTala = m_currentTalaIdx.load();
    if (curTala >= 0 && curTala < static_cast<int>(m_song.talas.size())) {
        for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[curTala].trackNotes.size(); ++t) {
            m_runtimeTracks[t].spec.notes = m_song.talas[curTala].trackNotes[t];
        }
    }
}

void SynthEngine::addHarmoniumSong(const HarmoniumSong& newSong) {
    QMutexLocker locker(&m_mutex);
    m_song.harmoniumSongs.push_back(newSong);
    m_song.currentHarmSongIdx = static_cast<int>(m_song.harmoniumSongs.size()) - 1;
    m_currentHarmSongIdx.store(m_song.currentHarmSongIdx);
    ProjectIO::rebuildCompiledPatterns(m_song);
    int curTala = m_currentTalaIdx.load();
    if (curTala >= 0 && curTala < static_cast<int>(m_song.talas.size())) {
        for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[curTala].trackNotes.size(); ++t) {
            m_runtimeTracks[t].spec.notes = m_song.talas[curTala].trackNotes[t];
        }
    }
}

HarmoniumSong SynthEngine::randomiseCurrentRagaSong() {
    QMutexLocker locker(&m_mutex);
    int sIdx = std::clamp(m_currentHarmSongIdx.load(), 0, static_cast<int>(m_song.harmoniumSongs.size()) - 1);
    HarmoniumSong gen = ProjectIO::generateRandomRagaSong(m_song.harmoniumSongs[sIdx]);
    m_song.harmoniumSongs[sIdx] = gen;
    ProjectIO::rebuildCompiledPatterns(m_song);

    int curTala = m_currentTalaIdx.load();
    if (curTala >= 0 && curTala < static_cast<int>(m_song.talas.size())) {
        m_song.totalLoopTicks = m_song.talas[curTala].loopTicks;
        for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[curTala].trackNotes.size(); ++t) {
            m_runtimeTracks[t].spec.notes = m_song.talas[curTala].trackNotes[t];
        }
    }
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
    return gen;
}

TalaTheka SynthEngine::randomiseCurrentTalaTheka() {
    QMutexLocker locker(&m_mutex);
    int tIdx = std::clamp(m_currentTalaIdx.load(), 0, static_cast<int>(m_song.talaThekas.size()) - 1);
    TalaTheka gen = ProjectIO::generateRandomTalaTheka(m_song.talaThekas[tIdx]);
    m_song.talaThekas[tIdx] = gen;
    ProjectIO::rebuildCompiledPatterns(m_song);
    m_drumLoopTicks.store(m_song.drumLoopTicks);

    for (size_t t = 0; t < m_runtimeTracks.size() && t < m_song.talas[tIdx].trackNotes.size(); ++t) {
        m_runtimeTracks[t].spec.notes = m_song.talas[tIdx].trackNotes[t];
    }
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
    return gen;
}

void SynthEngine::auditionTrack(int trackIdx) {
    QMutexLocker locker(&m_mutex);
    if (trackIdx < 0 || trackIdx >= static_cast<int>(m_runtimeTracks.size())) return;
    SongNote sn{0, 48, 69, 1.0f};
    double secPerTick = 60.0 / (std::max(40, m_bpm.load()) * 48.0);
    triggerTrackVoice(m_runtimeTracks[trackIdx], trackIdx, sn, secPerTick);
}

void SynthEngine::auditionBolByName(const QString& bolText) {
    QMutexLocker locker(&m_mutex);
    QString firstBol = bolText.trimmed().split(' ', Qt::SkipEmptyParts).value(0).toLower();
    if (firstBol.isEmpty() || firstBol == "-") return;
    double secPerTick = 60.0 / (std::max(40, m_bpm.load()) * 48.0);
    for (size_t i = 0; i < m_runtimeTracks.size(); ++i) {
        if (m_runtimeTracks[i].spec.name.toLower() == firstBol) {
            SongNote sn{0, 48, 69, 1.0f};
            triggerTrackVoice(m_runtimeTracks[i], static_cast<int>(i), sn, secPerTick);
            break;
        }
    }
}

void SynthEngine::auditionSwara(const QString& swaraText) {
    QString firstTok = swaraText.trimmed().split(' ', Qt::SkipEmptyParts).value(0);
    int midiKey = 60;
    if (!ProjectIO::parseSwaraToken(firstTok, midiKey)) return;

    QMutexLocker locker(&m_mutex);
    double secPerTick = 60.0 / (std::max(40, m_bpm.load()) * 48.0);
    for (size_t i = 0; i < m_runtimeTracks.size(); ++i) {
        auto& rt = m_runtimeTracks[i];
        if (!rt.spec.isBeatTrack && rt.spec.name.contains("Harmonium", Qt::CaseInsensitive)) {
            SongNote sn{0, 36, midiKey, 0.9f};
            triggerTrackVoice(rt, static_cast<int>(i), sn, secPerTick);
            m_detectedMidiNote.store(midiKey + m_saSemitone.load());
            break;
        }
    }
}

MmpSong SynthEngine::getSongSnapshot() {
    QMutexLocker locker(&m_mutex);
    MmpSong copy = m_song;
    copy.bpm = m_bpm.load();
    copy.saSemitone = m_saSemitone.load();
    copy.a4Hz = m_a4Hz.load();
    copy.currentTalaIdx = m_currentTalaIdx.load();
    copy.currentHarmSongIdx = m_currentHarmSongIdx.load();
    copy.drumLoopTicks = m_drumLoopTicks.load();
    for (size_t i = 0; i < m_runtimeTracks.size() && i < copy.tracks.size(); ++i) {
        copy.tracks[i].muted = m_runtimeTracks[i].spec.muted;
        copy.tracks[i].notes = m_runtimeTracks[i].spec.notes;
    }
    return copy;
}

void SynthEngine::setHarmoniumPresetMode(bool presetAutoOn) {
    QMutexLocker locker(&m_mutex);
    m_harmoniumPresetMode.store(presetAutoOn);
}

void SynthEngine::startWithCountdown(bool enableCountdown) {
    QMutexLocker locker(&m_mutex);
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
    m_isPaused.store(false);

    for (auto& rt : m_runtimeTracks) {
        for (auto& v : rt.voices) {
            if (v.active && v.midiOutNote >= 0 && m_midi) {
                m_midi->sendNoteOff(v.midiOutChan, v.midiOutNote);
            }
            v.active = false;
            v.midiOutNote = -1;
        }
    }

    if (enableCountdown) {
        m_countdownBeats.store(4);
        m_countdownTimerSec = 0.0;
        m_clickEnv = 1.0;
    } else {
        m_countdownBeats.store(0);
    }
    m_isPlaying.store(true);
}

void SynthEngine::stopPlayback() {
    QMutexLocker locker(&m_mutex);
    m_isPlaying.store(false);
    m_isPaused.store(false);
    m_countdownBeats.store(0);
    m_songTickPos = 0.0;
    m_lastTriggeredTick = -1;
    for (auto& rt : m_runtimeTracks) {
        for (auto& v : rt.voices) {
            if (v.active && v.midiOutNote >= 0 && m_midi) {
                m_midi->sendNoteOff(v.midiOutChan, v.midiOutNote);
            }
            v.active = false;
            v.midiOutNote = -1;
        }
    }
    if (m_midi && m_midiOutEnabled.load()) {
        m_midi->sendAllNotesOff();
    }
}

void SynthEngine::togglePause() {
    if (!m_isPlaying.load()) return;
    m_isPaused.store(!m_isPaused.load());
}

void SynthEngine::setBpm(int bpm) {
    m_bpm.store(std::clamp(bpm, 40, 260));
}

void SynthEngine::setTrackMuted(int trackIdx, bool muted) {
    QMutexLocker locker(&m_mutex);
    if (trackIdx >= 0 && trackIdx < static_cast<int>(m_runtimeTracks.size())) {
        m_runtimeTracks[trackIdx].spec.muted = muted;
    }
}

bool SynthEngine::isTrackMuted(int trackIdx) const {
    QMutexLocker locker(&m_mutex);
    if (trackIdx >= 0 && trackIdx < static_cast<int>(m_runtimeTracks.size())) {
        return m_runtimeTracks[trackIdx].spec.muted;
    }
    return false;
}

void SynthEngine::setTuning(int saSemitone, double a4Hz) {
    m_saSemitone.store((saSemitone % 12 + 12) % 12);
    m_a4Hz.store(std::clamp(a4Hz, 415.0, 466.0));
}

void SynthEngine::triggerDrumTrackFromMidi(int trackIdx, int velocity) {
    QMutexLocker locker(&m_mutex);
    if (trackIdx < 0 || trackIdx >= static_cast<int>(m_runtimeTracks.size())) return;
    // Prevent triggering the melodic Harmonium_Bellows track (index 16) as a drum
    if (m_runtimeTracks[trackIdx].spec.name.contains("Harmonium", Qt::CaseInsensitive)) return;

    float vel = std::clamp(velocity / 127.0f, 0.15f, 1.0f);
    int key = m_runtimeTracks[trackIdx].spec.isBeatTrack ? 69 : 60;
    SongNote sn{0, 48, key, vel};
    double secPerTick = 60.0 / (std::max(40, m_bpm.load()) * 48.0);
    triggerTrackVoice(m_runtimeTracks[trackIdx], trackIdx, sn, secPerTick);
}

void SynthEngine::handleIncomingMidi(int channel, int midiNote, int velocity, bool isNoteOn) {
    if (!m_midiInEnabled.load()) return;

    int mode = m_midiInMode.load();
    int harmCh = m_midiHarmChannel.load();
    int drumCh = m_midiDrumChannel.load();

    bool routeToHarm = false;
    bool routeToDrum = false;

    if (mode == 0) {
        // Dual Channel Mode (Harmonium + Drums on selectable channels)
        bool drumMatch = (drumCh == -1) || (channel == drumCh);
        bool harmMatch = (harmCh == -1) ? (channel != drumCh || drumCh == -1) : (channel == harmCh);
        routeToDrum = drumMatch;
        routeToHarm = harmMatch;
    } else if (mode == 1) {
        // Split Keyboard Mode: keys < 52 (C2..D#3) play Drums, keys >= 52 (E3+) play Harmonium
        if (midiNote < 52) routeToDrum = true;
        else routeToHarm = true;
    } else if (mode == 2) {
        // Harmonium Only
        if (harmCh == -1 || channel == harmCh) routeToHarm = true;
    } else if (mode == 3) {
        // Drums Only
        if (drumCh == -1 || channel == drumCh) routeToDrum = true;
    }

    if (routeToDrum && isNoteOn) {
        int trkIdx = midiNoteToDrumTrack(midiNote);
        triggerDrumTrackFromMidi(trkIdx, velocity);
    }

    if (routeToHarm) {
        if (isNoteOn) noteOn(midiNote, velocity, true);
        else noteOff(midiNote, true);
    }
}

void SynthEngine::noteOn(int midiNote, int velocity, bool fromExternalMidi) {
    QMutexLocker locker(&m_mutex);
    int shiftedNote = std::clamp(midiNote + m_octaveShift.load() * 12, 12, 115);

    int ragaSrc = m_ragaSourceTrack.load();
    if (!m_isPlaying.load() || !m_harmoniumPresetMode.load() || ragaSrc == -1 || ragaSrc == -2) {
        m_detectedMidiNote.store(shiftedNote);
    }

    // Forward live touch / PC keyboard notes to MIDI Out if enabled
    if (!fromExternalMidi && m_midi && m_midiOutEnabled.load()) {
        int outMode = m_midiOutMode.load();
        if (outMode == 0 || outMode == 1) {
            int outCh = (m_midiHarmChannel.load() >= 0) ? m_midiHarmChannel.load() : 0;
            m_midi->sendNoteOn(outCh, shiftedNote, velocity);
        }
    }

    for (auto& hv : m_harmVoices) {
        if (hv.active && hv.held && hv.midiNote == shiftedNote) {
            hv.velocity = velocity / 127.0f;
            return;
        }
    }

    HarmoniumVoice* slot = nullptr;
    double maxTrel = -1.0;


    for (auto& hv : m_harmVoices) {
        if (!hv.active) { slot = &hv; break; }
        if (!hv.held && hv.trel > maxTrel) {
            maxTrel = hv.trel;
            slot = &hv;
        }
    }

    if (!slot) {
        double maxT = -1.0;
        for (auto& hv : m_harmVoices) {
            if (hv.t > maxT) {
                maxT = hv.t;
                slot = &hv;
            }
        }
    }
    if (!slot) slot = &m_harmVoices[0];

    slot->active = true;
    slot->held = true;
    slot->midiNote = shiftedNote;
    slot->velocity = std::clamp(velocity / 127.0f, 0.1f, 1.0f);
    slot->t = 0.0;
    slot->trel = 0.0;
    slot->lastOut = 0.0;
    slot->intState.fill(0.0);
}

void SynthEngine::noteOff(int midiNote, bool fromExternalMidi) {
    QMutexLocker locker(&m_mutex);
    int shiftedNote = std::clamp(midiNote + m_octaveShift.load() * 12, 12, 115);

    if (!fromExternalMidi && m_midi && m_midiOutEnabled.load()) {
        int outMode = m_midiOutMode.load();
        if (outMode == 0 || outMode == 1) {
            int outCh = (m_midiHarmChannel.load() >= 0) ? m_midiHarmChannel.load() : 0;
            m_midi->sendNoteOff(outCh, shiftedNote);
        }
    }

    for (auto& hv : m_harmVoices) {
        if (hv.active && hv.held && hv.midiNote == shiftedNote) {
            hv.held = false;
            hv.trel = 0.0;
        }
    }
}

double SynthEngine::midiToFreq(int midiNote) const {
    return m_a4Hz.load() * std::pow(2.0, (midiNote - 69) / 12.0);
}

void SynthEngine::triggerTrackVoice(RuntimeTrack& rt, int trackIdx, const SongNote& sn, double secPerTick) {
    auto& v = rt.voices[rt.nextVoice];
    rt.nextVoice = (rt.nextVoice + 1) % static_cast<int>(rt.voices.size());

    // If stealing an active voice that had a pending MIDI Out note, close it cleanly first
    if (v.active && v.midiOutNote >= 0 && m_midi) {
        m_midi->sendNoteOff(v.midiOutChan, v.midiOutNote);
        v.midiOutNote = -1;
    }

    v.active = true;
    v.key = sn.key;
    v.vel = sn.vol;
    v.noteTime = 0.0;
    v.released = false;
    v.relTime = 0.0;
    v.lastOut1 = 0.0;
    v.lastOut2 = 0.0;
    v.noteDurSec = (sn.lenTicks > 0) ? (sn.lenTicks * secPerTick) : 0.45;
    std::fill(v.intState1.begin(), v.intState1.end(), 0.0);
    std::fill(v.intState2.begin(), v.intState2.end(), 0.0);

    // Dispatch MIDI Out for sequenced / auditioned track notes
    if (m_midi && m_midiOutEnabled.load()) {
        int outMode = m_midiOutMode.load();
        bool isHarmTrack = !rt.spec.isBeatTrack && rt.spec.name.contains("Harmonium", Qt::CaseInsensitive);
        int velInt = std::clamp(static_cast<int>(sn.vol * 110.0f), 1, 127);

        if (isHarmTrack && (outMode == 0 || outMode == 1)) {
            int outCh = (m_midiHarmChannel.load() >= 0) ? m_midiHarmChannel.load() : 0;
            int outNote = std::clamp(sn.key + m_saSemitone.load(), 0, 127);
            v.midiOutChan = outCh;
            v.midiOutNote = outNote;
            m_midi->sendNoteOn(outCh, outNote, velInt);
        } else if (!isHarmTrack && (outMode == 0 || outMode == 2)) {
            int outCh = (m_midiDrumChannel.load() >= 0) ? m_midiDrumChannel.load() : 9;
            int outNote = drumTrackToMidiNote(trackIdx);
            v.midiOutChan = outCh;
            v.midiOutNote = outNote;
            m_midi->sendNoteOn(outCh, outNote, velInt);
        }
    }
}

qint64 SynthEngine::bytesAvailable() const {
    if (!isOpen() || !m_audioSink) return 0;
    return m_audioSink->bufferSize() + QIODevice::bytesAvailable();
}

qint64 SynthEngine::writeData(const char *data, qint64 len) {
    Q_UNUSED(data);
    return len;
}

qint64 SynthEngine::readData(char *data, qint64 maxlen) {
    QElapsedTimer dspTimer;
    dspTimer.start();

    QMutexLocker locker(&m_mutex);
    std::memset(data, 0, maxlen);

    int channels = m_format.channelCount();
    if (channels <= 0) return maxlen;

    bool isFloat = (m_format.sampleFormat() == QAudioFormat::Float);
    bool isInt16 = (m_format.sampleFormat() == QAudioFormat::Int16);
    if (!isFloat && !isInt16) return maxlen;

    int bytesPerSample = isFloat ? sizeof(float) : sizeof(qint16);
    int frames = maxlen / (bytesPerSample * channels);

    float *bufferF32 = reinterpret_cast<float*>(data);
    qint16 *bufferI16 = reinterpret_cast<qint16*>(data);

    const double dt = 1.0 / m_sampleRate;
    const double currentBpm = static_cast<double>(m_bpm.load());
    const double beatDurationSec = 60.0 / currentBpm;
    const double ticksPerSec = (currentBpm / 60.0) * 48.0;
    const double secPerTick = 1.0 / ticksPerSec;
    const bool presetHarmoniumOn = m_harmoniumPresetMode.load();
    const int drumLoop = std::max(48, m_drumLoopTicks.load());
    const int saOffset = m_saSemitone.load();

    int swaraMode = m_droneSwaraMode.load();
    int midSemitoneOffset = -5;
    if (swaraMode == 1) midSemitoneOffset = -7;
    else if (swaraMode == 2) midSemitoneOffset = -1;
    else if (swaraMode == 3) midSemitoneOffset = 0;

    const int droneMidiNotes[3] = {
        60 + saOffset,
        60 + saOffset + midSemitoneOffset,
        48 + saOffset
    };
    const double droneVols[3] = {0.80, 0.75, 0.85};

    for (int i = 0; i < frames; ++i) {
        double mix = 0.0;

        if (m_isPlaying.load() && !m_isPaused.load()) {
            int cdBeats = m_countdownBeats.load();
            if (cdBeats > 0) {
                if (m_clickEnv > 0.001) {
                    double clickFreq = (cdBeats == 1) ? 1200.0 : 880.0;
                    mix += std::sin(2.0 * M_PI * clickFreq * m_countdownTimerSec) * m_clickEnv * 0.4;
                    m_clickEnv *= std::exp(-dt * 35.0);
                }
                m_countdownTimerSec += dt;
                if (m_countdownTimerSec >= beatDurationSec) {
                    m_countdownTimerSec -= beatDurationSec;
                    cdBeats--;
                    m_countdownBeats.store(cdBeats);
                    if (cdBeats > 0) m_clickEnv = 1.0;
                }
            } else {
                int curTick = static_cast<int>(std::floor(m_songTickPos));
                if (curTick != m_lastTriggeredTick) {
                    m_lastTriggeredTick = curTick;
                    int drumTick = curTick % drumLoop;
                    m_currentDrumTick.store(drumTick);
                    m_currentSongTick.store(curTick);
                    int ragaSrc = m_ragaSourceTrack.load();

                    for (size_t tIdx = 0; tIdx < m_runtimeTracks.size(); ++tIdx) {
                        auto& rt = m_runtimeTracks[tIdx];
                        if (rt.spec.muted) continue;
                        if (!rt.spec.isBeatTrack && !presetHarmoniumOn) continue;

                        int matchTick = rt.spec.isBeatTrack ? drumTick : curTick;
                        for (const auto& sn : rt.spec.notes) {
                            if (sn.startTick == matchTick) {
                                triggerTrackVoice(rt, static_cast<int>(tIdx), sn, secPerTick);
                                if (ragaSrc == static_cast<int>(tIdx) || (ragaSrc == -1 && !rt.spec.isBeatTrack)) {
                                    m_detectedMidiNote.store(sn.key + saOffset);
                                }
                            }
                        }
                    }
                }

                m_songTickPos += ticksPerSec * dt;
                if (m_songTickPos >= m_song.totalLoopTicks) {
                    m_songTickPos = std::fmod(m_songTickPos, static_cast<double>(m_song.totalLoopTicks));
                    m_lastTriggeredTick = -1;
                }
            }
        }

        for (auto& rt : m_runtimeTracks) {
            if (rt.spec.muted || !rt.spec.o1Ast) continue;
            if (!rt.spec.isBeatTrack && m_isPlaying.load() && !presetHarmoniumOn &&
                rt.spec.name.contains("Harmonium", Qt::CaseInsensitive)) continue;

            double trkSample = 0.0;
            for (auto& v : rt.voices) {
                if (!v.active) continue;

                if (!v.released && v.noteTime >= v.noteDurSec) {
                    v.released = true;
                    v.relTime = 0.0;
                    if (v.midiOutNote >= 0 && m_midi) {
                        m_midi->sendNoteOff(v.midiOutChan, v.midiOutNote);
                        v.midiOutNote = -1;
                    }
                }
                if (v.released && v.relTime > std::max(0.35, rt.spec.rel)) {
                    v.active = false;
                    continue;
                }

                XpressiveParser::Env env;
                env.t = v.noteTime;
                env.tempo = currentBpm;
                env.srate = m_sampleRate;
                int effectiveMidi = rt.spec.isBeatTrack ? (69 + (v.key - rt.spec.baseNote)) : (v.key + saOffset);
                env.f = midiToFreq(effectiveMidi);
                env.rel = v.released ? 1.0 : 0.0;
                env.trel = v.relTime;
                env.A1 = rt.spec.A1;
                env.A2 = rt.spec.A2;
                env.A3 = rt.spec.A3;
                env.w1 = rt.spec.w1Ast.get();
                env.w2 = rt.spec.w2Ast.get();
                env.w3 = rt.spec.w3Ast.get();
                env.w1Sample = &rt.spec.w1Sample;
                env.w2Sample = &rt.spec.w2Sample;
                env.w3Sample = &rt.spec.w3Sample;

                env.lastOut = v.lastOut1;
                env.intState = v.intState1.data();
                env.numIntegrators = static_cast<int>(v.intState1.size());
                double val1 = rt.spec.o1Ast->eval(env);
                v.lastOut1 = val1;

                double val = val1;
                if (rt.spec.o2Ast && !rt.spec.o2Str.isEmpty()) {
                    env.lastOut = v.lastOut2;
                    env.intState = v.intState2.data();
                    double val2 = rt.spec.o2Ast->eval(env);
                    v.lastOut2 = val2;
                    val = 0.5 * (val1 + val2);
                }

                trkSample += val * v.vel;
                v.noteTime += dt;
                if (v.released) v.relTime += dt;
            }

            for (auto& bf : rt.filters) {
                trkSample = bf.process(trkSample);
            }

            // Apply the dynamic transfer function convolution filter block
            trkSample = rt.tfFx.process(trkSample);

            double gain = rt.spec.isBeatTrack ? 0.35 : 0.22;
            mix += trkSample * rt.spec.vol * gain;
        }

        if (m_droneEnabled.load()) {
            int dPreset = m_dronePreset.load();
            bool useShruti = (dPreset == 0 || dPreset == 2);
            bool useTanpura = (dPreset == 1 || dPreset == 2);
            double droneSample = 0.0;

            for (int dn = 0; dn < 3; ++dn) {
                auto& dState = m_droneNotes[dn];
                XpressiveParser::Env env;
                env.t = dState.t;
                env.tempo = currentBpm;
                env.srate = m_sampleRate;
                env.f = midiToFreq(droneMidiNotes[dn]);
                env.rel = 0.0;
                env.trel = 0.0;

                double noteOut = 0.0;
                if (useShruti && m_shrutiO1 && m_shrutiO2) {
                    env.intState = dState.shrutiInt1.data();
                    env.numIntegrators = static_cast<int>(dState.shrutiInt1.size());
                    double s1 = m_shrutiO1->eval(env);
                    env.intState = dState.shrutiInt2.data();
                    double s2 = m_shrutiO2->eval(env);
                    noteOut += 0.5 * (s1 + s2) * 0.938;
                }
                if (useTanpura && m_tanpuraO1 && m_tanpuraO2) {
                    env.intState = dState.tanpuraInt1.data();
                    env.numIntegrators = static_cast<int>(dState.tanpuraInt1.size());
                    double t1 = m_tanpuraO1->eval(env);
                    env.intState = dState.tanpuraInt2.data();
                    double t2 = m_tanpuraO2->eval(env);
                    noteOut += 0.5 * (t1 + t2) * 0.35;
                }

                droneSample += noteOut * droneVols[dn];
                dState.t += dt;
            }
            mix += droneSample * 0.22;
        }

        if (m_harmoniumAst) {
            double harmMix = 0.0;
            for (auto& hv : m_harmVoices) {
                if (!hv.active) continue;

                if (!hv.held) {
                    hv.trel += dt;
                    if (hv.trel > 0.45) {
                        hv.active = false;
                        continue;
                    }
                }

                XpressiveParser::Env env;
                env.t = hv.t;
                env.tempo = currentBpm;
                env.srate = m_sampleRate;
                env.f = midiToFreq(hv.midiNote);
                env.rel = hv.held ? 0.0 : 1.0;
                env.trel = hv.trel;
                env.lastOut = hv.lastOut;
                env.intState = hv.intState.data();
                env.numIntegrators = static_cast<int>(hv.intState.size());

                double vOut = m_harmoniumAst->eval(env);
                hv.lastOut = vOut;
                harmMix += vOut * hv.velocity;
                hv.t += dt;
            }

            // ADDED: Process the live manual keys through the EQ/Chorus!
            harmMix = m_liveHarmoniumTf.process(harmMix);

            mix += harmMix * 0.22;
        }

        float outSample = static_cast<float>(mix);
        if (std::isnan(outSample) || std::isinf(outSample)) outSample = 0.0f;
        outSample = std::tanh(outSample);
        
        if (isFloat) {
            for (int ch = 0; ch < channels; ++ch) bufferF32[i * channels + ch] = outSample;
        } else {
            qint16 iSample = static_cast<qint16>(outSample * 32767.0f);
            for (int ch = 0; ch < channels; ++ch) bufferI16[i * channels + ch] = iSample;
        }
    }


    if (frames > 0) {
        double bufferTime = static_cast<double>(frames) / m_sampleRate;
        double calcTime = dspTimer.nsecsElapsed() / 1e9;
        float load = static_cast<float>((calcTime / bufferTime) * 100.0);
        float current = m_dspLoad.load(std::memory_order_relaxed);
        m_dspLoad.store(current * 0.95f + load * 0.05f, std::memory_order_relaxed); // Smooth display
    }


    return maxlen;
}