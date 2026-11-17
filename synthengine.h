#ifndef SYNTHENGINE_H
#define SYNTHENGINE_H

#include <atomic>
#include <vector>
#include <array>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include "xpressive_parser.h"
#include "effect.h"
#include "drum_presets.h"

#ifdef ESP32
#include <esp_heap_caps.h>
inline void* allocAudioRam(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#else
inline void* allocAudioRam(size_t bytes) {
    return std::malloc(bytes);
}
#endif

template <typename T>
inline const T& my_clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

inline float fastExpNeg(float x) {
    if (x <= 0.0f) return 1.0f;
    if (x >= 16.0f) return 0.0f;
    float t = 1.0f + x * (1.0f / 8.0f);
    t *= t; t *= t; t *= t;
    return 1.0f / t;
}

inline float fastTanh(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

class SynthEngine {
public:
    static constexpr int NUM_TRACKS        = 24;
    static constexpr int NUM_BAKED_DRUMS   = 22; // 0..19 Percussion, 20 Shankha 1, 21 Shankha 2
    static constexpr int TRACK_HARMONIUM   = 22;
    static constexpr int TRACK_SINGING     = 23;
    static constexpr int VOCAL_CHAMBER_LEN = 4096;

    SynthEngine() : m_liveHarmoniumTf(22050.0f) {}

    void init(float sampleRate = 22050.0f) {
        m_srate = std::max(8000.0f, sampleRate);

        // Precompute float frequencies and Q32 fixed-point phase increments (2^32 / m_srate)
        const double q32Scale = 4294967296.0 / static_cast<double>(m_srate);
        m_lfoBpmPhaseScale = static_cast<uint32_t>(q32Scale / 120.0);

        for (int m = 0; m < 128; ++m) {
            float f = m_a4Hz.load() * std::pow(2.0f, (m - 69) * (1.0f / 12.0f));
            m_midiFreqLut[m]       = f;
            m_midiPhaseIncLut[m]   = static_cast<uint32_t>(f * q32Scale);
            m_midiCelesteIncLut[m] = static_cast<uint32_t>((f * 1.0046316f) * q32Scale); // +8 cents
        }

        // Exact 1-pole fixed-point envelope multipliers for (1 - exp(-8*t)) and exp(-18*trel)
        m_attackStepQ15 = static_cast<int32_t>(std::lround((1.0f - std::exp(-8.0f / m_srate)) * 32768.0f));
        m_relDecayQ15   = static_cast<int32_t>(std::lround(std::exp(-18.0f / m_srate) * 32768.0f));

        m_liveHarmoniumTf.setSampleRate(m_srate);
        m_liveHarmoniumTf.enabled = false;

        initPrecompiledFormantLut();
        initVocalChamber();

        m_harmoniumAst = XpressiveParser::parse(BuiltInPresets::defaultHarmoniumO1());
        m_singingAst   = XpressiveParser::parse(BuiltInPresets::defaultSingingVoiceO1());
        m_shrutiO1     = XpressiveParser::parse(BuiltInPresets::shrutiDroneO1());
        m_shrutiO2     = XpressiveParser::parse(BuiltInPresets::shrutiDroneO2());
        m_tanpuraO1    = XpressiveParser::parse(BuiltInPresets::tanpuraDroneO1());
        m_tanpuraO2    = XpressiveParser::parse(BuiltInPresets::tanpuraDroneO2());

        const auto& trkSpecs = BuiltInPresets::getTracks();
        for (int i = 0; i < NUM_TRACKS && i < static_cast<int>(trkSpecs.size()); ++i) {
            m_trackMuted[i] = trkSpecs[i].muted;
        }
        m_userSingingEnabled.store(false);

        m_bakedDrumLen = static_cast<int>(m_srate * 1.25f);
        for (int t = 0; t < NUM_BAKED_DRUMS; ++t) {
            m_bakedDrums[t] = static_cast<int16_t*>(allocAudioRam(m_bakedDrumLen * sizeof(int16_t)));
        }
        rebakeDrumsInPsram();

        m_maxDroneLoopSamples = static_cast<int>(m_srate * 7.0f);
        m_droneLoopBuf = static_cast<int16_t*>(allocAudioRam((m_maxDroneLoopSamples + 2048) * sizeof(int16_t)));
        m_droneDirty.store(true);

        rebuildActivePatterns();
    }

    void randomizeRaga() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& songs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs());
        auto& hs = songs[m_currentHarmSongIdx.load()];

        if (hs.forms.empty()) return;
        auto& form = hs.forms[0];

        std::vector<int> pc;
        for (const char* cell : form.swaraCells) {
            int k = 60;
            if (BuiltInPresets::parseSwaraToken(cell, k)) {
                int p = ((k - 60) % 12 + 12) % 12;
                if (std::find(pc.begin(), pc.end(), p) == pc.end()) pc.push_back(p);
            }
        }
        if (pc.size() < 5) pc = {0, 2, 4, 5, 7, 9, 11};

        std::vector<int> ladder;
        for (int midi = 55; midi <= 76; ++midi) {
            if (std::find(pc.begin(), pc.end(), ((midi - 60) % 12 + 12) % 12) != pc.end()) ladder.push_back(midi);
        }

        int minIdx = 0;
        int maxIdx = static_cast<int>(ladder.size()) - 1;
        int curIdx = maxIdx / 2;
        int matras = BuiltInPresets::parseTotalMatras(hs.divisions);

        form.swaraCells.clear();
        form.lyricCells.clear();

        auto getSwaraLit = [](int midi) -> const char* {
            static const char* lits[] = {"'P", "'d", "'D", "'n", "'N", "S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N", "S'", "r'", "R'", "g'"};
            int offsets[] = {55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75};
            for (int i = 0; i < 21; ++i) if (offsets[i] == midi) return lits[i];
            return "-";
        };

        for (int r = 0; r < form.numRows; ++r) {
            for (int m = 0; m < matras; ++m) {
                if (r == form.numRows - 1 && m == matras - 1) {
                    form.swaraCells.push_back("-");
                } else {
                    int step = 0;
                    int dirRoll = std::rand() % 100;
                    if (dirRoll < 42) step = 1;
                    else if (dirRoll < 84) step = -1;
                    else if (dirRoll < 92) step = 2;
                    else step = -2;

                    curIdx = my_clamp(curIdx + step, minIdx, maxIdx);
                    form.swaraCells.push_back(getSwaraLit(ladder[curIdx]));
                }
            }
        }
    }

    void randomizeTala() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& talas = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas());
        auto& kt = talas[m_currentTalaIdx.load()];

        std::vector<int> vibhags;
        const char* p = kt.divisions;
        while (p && *p) {
            while (*p && !std::isdigit(static_cast<unsigned char>(*p))) p++;
            if (!*p) break;
            int v = std::strtol(p, const_cast<char**>(&p), 10);
            if (v > 0) vibhags.push_back(v);
        }
        if (vibhags.empty()) vibhags = {4, 4};

        int totalMatras = 0;
        for (int v : vibhags) totalMatras += v;

        kt.bolCells.clear();
        kt.kartalCells.clear();

        const char* samBols[] = {"Dha", "Dhin"};
        const char* taliStart[] = {"Dha", "Dhin", "Ge", "Ta"};
        const char* khaliStart[] = {"Ti", "Ta", "Na", "Ka"};
        const char* bodyBols[] = {"Dhin", "Dha", "Ge", "Na", "Ti", "Ta"};

        int globalMatra = 0;
        for (size_t v = 0; v < vibhags.size(); ++v) {
            bool isSam = (v == 0);
            bool isKhali = (v == vibhags.size() / 2);
            for (int m = 0; m < vibhags[v]; ++m) {
                int remaining = totalMatras - 1 - globalMatra;
                const char* bol = "Dha";
                const char* kar = "Kartal Closed";

                if (isSam && m == 0) { bol = samBols[std::rand() % 2]; kar = "Kartal Open"; }
                else if (remaining == 0) { bol = "Ti Re Ka Ta"; kar = "Closed+Closed"; }
                else if (m == 0) {
                    bol = isKhali ? khaliStart[std::rand() % 4] : taliStart[std::rand() % 4];
                    kar = isKhali ? "Kartal Closed" : "Kartal Open";
                } else {
                    bol = bodyBols[std::rand() % 6];
                    if (std::rand() % 100 > 25) kar = "Kartal Closed";
                    else kar = "-";
                }
                kt.bolCells.push_back(bol);
                kt.kartalCells.push_back(kar);
                globalMatra++;
            }
        }
    }

    void selectTala(int idx) {
        int count = static_cast<int>(BuiltInPresets::getTalaThekas().size());
        if (count > 0) m_currentTalaIdx.store((idx % count + count) % count);
        rebuildActivePatterns();
    }

    void selectHarmoniumSong(int idx) {
        int count = static_cast<int>(BuiltInPresets::getHarmoniumSongs().size());
        if (count > 0) {
            m_currentHarmSongIdx.store((idx % count + count) % count);
            m_bpm.store(BuiltInPresets::getHarmoniumSongs()[m_currentHarmSongIdx.load()].defaultBpm);
        }
        m_droneDirty.store(true);
        rebuildActivePatterns();
    }

    void handleIncomingMidi(int midiNote, int velocity, bool isNoteOn) {
        int mode = m_midiInMode.load();
        bool routeToHarm = false, routeToDrum = false;

        if (mode == 0) { routeToDrum = true; routeToHarm = true; }
        else if (mode == 1) { if (midiNote < 52) routeToDrum = true; else routeToHarm = true; }
        else if (mode == 2) { routeToHarm = true; }
        else if (mode == 3) { routeToDrum = true; }

        if (routeToDrum && isNoteOn) {
            int trkIdx = (midiNote >= 36 && midiNote < 36 + NUM_BAKED_DRUMS)
            ? (midiNote - 36)
            : ((midiNote - 36) % NUM_BAKED_DRUMS + NUM_BAKED_DRUMS) % NUM_BAKED_DRUMS;
            std::lock_guard<std::mutex> lock(m_mutex);
            triggerBakedDrum(trkIdx);
        }
        if (routeToHarm) {
            if (isNoteOn) noteOn(midiNote, velocity);
            else noteOff(midiNote);
        }
    }

    void rebakeDrumsInPsram() {
        const auto& trkSpecs = BuiltInPresets::getTracks();
        const float dt = 1.0f / m_srate;
        const float fA4 = m_a4Hz.load();

        for (int t = 0; t < NUM_BAKED_DRUMS && t < static_cast<int>(trkSpecs.size()); ++t) {
            if (!m_bakedDrums[t]) continue;
            auto o1 = XpressiveParser::parse(trkSpecs[t].o1Str);
            auto o2 = (trkSpecs[t].o2Str && trkSpecs[t].o2Str[0])
                          ? XpressiveParser::parse(trkSpecs[t].o2Str)
                          : nullptr;

            float int1[16] = {0}, int2[16] = {0};
            float last1 = 0.0f, last2 = 0.0f;

            for (int s = 0; s < m_bakedDrumLen; ++s) {
                XpressiveParser::Env env;
                env.t = s * dt;
                env.tempo = 140.0f;
                env.srate = m_srate;
                env.f = fA4;
                env.lastOut = last1;
                env.intState = int1;
                env.numIntegrators = 16;

                float v1 = o1->eval(env);
                last1 = v1;
                float val = v1;

                if (o2) {
                    env.lastOut = last2;
                    env.intState = int2;
                    float v2 = o2->eval(env);
                    last2 = v2;
                    val = 0.5f * (v1 + v2);
                }
                val = my_clamp(val * 0.38f, -1.0f, 1.0f);
                m_bakedDrums[t][s] = static_cast<int16_t>(val * 32767.0f);
            }
            #ifdef ESP32
               vTaskDelay(1);
            #endif
        }
    }

    void rebakeDroneLoopInPsram() {
        if (!m_droneLoopBuf) return;
        const auto& lut = XpressiveParser::lut();
        float bpm = static_cast<float>(m_bpm.load());
        int cycleSamples = std::min(m_maxDroneLoopSamples, static_cast<int>((240.0f / bpm) * m_srate));
        if (cycleSamples < 4096) cycleSamples = 4096;
        const int fadeLen = 2048;

        int saOffset = m_saSemitone.load();
        int swaraMode = m_droneSwaraMode.load();
        int midOffset = (swaraMode == 1) ? -7 : (swaraMode == 2) ? -1 : (swaraMode == 3) ? 0 : -5;

        const float freqs[3] = {
            midiToFreq(60 + saOffset),
            midiToFreq(60 + saOffset + midOffset),
            midiToFreq(48 + saOffset)
        };
        const float droneVols[3] = { 0.80f, 0.75f, 0.85f };
        int dPreset = m_dronePreset.load();
        const bool useShruti  = (dPreset == 0 || dPreset == 2);
        const bool useTanpura = (dPreset == 1 || dPreset == 2);

        const float dt = 1.0f / m_srate;
        const float lfoRate = bpm * (1.0f / 60.0f);
        const float tanpuraRate = bpm * (1.0f / 120.0f);
        const float tanpuraModRate = bpm * (1.0f / 240.0f);

        float pBase[3] = {0.0f, 0.0f, 0.0f};
        float pDetMinus[3] = {0.0f, 0.0f, 0.0f};
        float pDetPlus[3]  = {0.0f, 0.0f, 0.0f};

        for (int s = 0; s < cycleSamples + fadeLen; ++s) {
            float t = 1.0f + s * dt;
            float sampleSum = 0.0f;
            float shrutiLfo = 0.92f + 0.08f * lut.sinUnit(t * lfoRate);
            float pluckPhase1 = t * tanpuraRate;
            pluckPhase1 -= std::floor(pluckPhase1);
            float pluckPhase2 = pluckPhase1 + 0.5f;
            if (pluckPhase2 >= 1.0f) pluckPhase2 -= 1.0f;
            float pluckEnv1 = fastExpNeg(pluckPhase1 * 2.2f);
            float pluckEnv2 = fastExpNeg(pluckPhase2 * 2.2f);
            float fmMod1 = 0.6f + 0.35f * lut.sinUnit(t * tanpuraModRate);
            float fmMod2 = 0.6f + 0.35f * lut.cosRad(t * tanpuraModRate);

            for (int dn = 0; dn < 3; ++dn) {
                float f = freqs[dn];
                pBase[dn]     += f * dt;
                pDetMinus[dn] += (f * 0.997116f) * dt;
                pDetPlus[dn]  += (f * 1.002892f) * dt;

                float noteOut = 0.0f;
                if (useShruti) {
                    float saw1 = 2.0f * (pDetMinus[dn] - std::floor(pDetMinus[dn])) - 1.0f;
                    float saw2 = 2.0f * (pDetPlus[dn]  - std::floor(pDetPlus[dn]))  - 1.0f;
                    float sq1  = ((pDetPlus[dn]  - std::floor(pDetPlus[dn]))  < 0.5f) ? 1.0f : -1.0f;
                    float sq2  = ((pDetMinus[dn] - std::floor(pDetMinus[dn])) < 0.5f) ? 1.0f : -1.0f;
                    float oct  = lut.sinUnit(2.0f * pBase[dn]);
                    float shruti = 0.3f * (0.275f * (saw1 + saw2) + 0.175f * (sq1 + sq2) + 0.2f * oct) * shrutiLfo;
                    noteOut += shruti * 0.938f;
                }
                if (useTanpura) {
                    float s2 = lut.sinUnit(2.0f * pBase[dn]);
                    float s3 = lut.sinUnit(3.0f * pBase[dn]);
                    float sawDet = 2.0f * (pDetMinus[dn] - std::floor(pDetMinus[dn])) - 1.0f;
                    float sawBase = 2.0f * (pBase[dn] - std::floor(pBase[dn])) - 1.0f;
                    float t1 = 0.35f * (lut.sinUnit(pBase[dn] + fmMod1 * s2 + 0.25f * s3) + 0.4f * sawDet * pluckEnv1);
                    float t2 = 0.35f * (lut.sinUnit(pDetPlus[dn] + fmMod2 * s2 + 0.25f * s3) + 0.4f * sawBase * pluckEnv2);
                    noteOut += 0.5f * (t1 + t2) * 0.35f;
                }
                sampleSum += noteOut * droneVols[dn];
            }

            float outVal = my_clamp(sampleSum * 0.22f, -1.0f, 1.0f);
            m_droneLoopBuf[s] = static_cast<int16_t>(outVal * 32767.0f);
        }

        for (int i = 0; i < fadeLen; ++i) {
            float alpha = static_cast<float>(i) * (1.0f / static_cast<float>(fadeLen));
            float head = static_cast<float>(m_droneLoopBuf[i]);
            float tail = static_cast<float>(m_droneLoopBuf[cycleSamples + i]);
            m_droneLoopBuf[i] = static_cast<int16_t>(alpha * head + (1.0f - alpha) * tail);
        }

        m_droneLoopLen = cycleSamples;
        m_droneReadIdx = 0;
        m_droneDirty.store(false);
    }

    void rebuildActivePatterns() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& talas = BuiltInPresets::getTalaThekas();
        auto& songs = BuiltInPresets::getHarmoniumSongs();
        int tIdx = my_clamp(m_currentTalaIdx.load(), 0, static_cast<int>(talas.size()) - 1);
        int sIdx = my_clamp(m_currentHarmSongIdx.load(), 0, static_cast<int>(songs.size()) - 1);
        const TalaTheka& kt = talas[tIdx];
        const HarmoniumSong& hs = songs[sIdx];

        for (int i = 0; i < NUM_TRACKS; ++i) m_trackNotes[i].clear();
        m_precompiledVocalPlans.clear();

        bool songHasLyrics = false;
        for (const auto& form : hs.forms) {
            if (!form.lyricCells.empty()) {
                songHasLyrics = true;
                break;
            }
        }

        int songMatrasPerRow = BuiltInPresets::parseTotalMatras(hs.divisions);
        int currentMatraGlobal = 0;
        std::vector<SongNote>& harmNotes = m_trackNotes[TRACK_HARMONIUM];
        std::vector<SongNote>& singNotes = m_trackNotes[TRACK_SINGING];

        for (const auto& form : hs.forms) {
            int formCells = form.numRows * songMatrasPerRow;
            for (int c = 0; c < formCells; ++c) {
                const char* cellText = (c < static_cast<int>(form.swaraCells.size())) ? form.swaraCells[c] : "-";
                const char* lyricText = (songHasLyrics && c < static_cast<int>(form.lyricCells.size())) ? form.lyricCells[c] : nullptr;
                int cellStartTick = currentMatraGlobal * 48;

                if (!cellText || std::strcmp(cellText, "-") == 0 || cellText[0] == '\0') {
                    if (!harmNotes.empty()) harmNotes.back().lenTicks += 48;
                    if (songHasLyrics && !singNotes.empty()) singNotes.back().lenTicks += 48;
                } else if (std::strcmp(cellText, ".") != 0) {
                    char buf[32];
                    std::strncpy(buf, cellText, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    char* tok = std::strtok(buf, " ");
                    std::vector<const char*> subTokens;
                    while (tok) { subTokens.push_back(tok); tok = std::strtok(nullptr, " "); }

                    auto pushVocalNote = [&](int stTick, int nLen, int midiKey, const char* rawLyric) {
                        bool isMelisma = (!rawLyric || !rawLyric[0] || std::strcmp(rawLyric, "-") == 0 || std::strcmp(rawLyric, ".") == 0);
                        VocalSyllablePlan plan;
                        if (isMelisma && !m_precompiledVocalPlans.empty()) {
                            VocalSyllablePlan& prev = m_precompiledVocalPlans.back();
                            plan = makeLegatoContinuation(prev);
                            prev.codaType = 0; 
                        } else {
                            const char* syl = isMelisma ? midiToSargamSyllable(midiKey - 60) : rawLyric;
                            plan = compileSanskritSyllable(syl);
                        }
                        int planIdx = static_cast<int>(m_precompiledVocalPlans.size());
                        m_precompiledVocalPlans.push_back(plan);
                        singNotes.push_back(SongNote(stTick, nLen, midiKey, 1.0f,
                                                     plan.vowelA1, plan.noiseAmp, static_cast<float>(planIdx)));
                    };

                    if (subTokens.size() == 1) {
                        int midiKey = 60;
                        if (BuiltInPresets::parseSwaraToken(subTokens[0], midiKey)) {
                            harmNotes.push_back(SongNote(cellStartTick, 46, midiKey, 1.0f, 0.0f, 0.0f, 0.5f));
                            if (songHasLyrics) {
                                pushVocalNote(cellStartTick, 46, midiKey, lyricText);
                            }
                        }
                    } else if (subTokens.size() >= 2) {
                        int subLen = 48 / static_cast<int>(subTokens.size());
                        for (size_t st = 0; st < subTokens.size(); ++st) {
                            if (std::strcmp(subTokens[st], "-") == 0) {
                                if (!harmNotes.empty()) harmNotes.back().lenTicks += subLen;
                                if (songHasLyrics && !singNotes.empty()) singNotes.back().lenTicks += subLen;
                            } else {
                                int midiKey = 60;
                                if (BuiltInPresets::parseSwaraToken(subTokens[st], midiKey)) {
                                    int stTick = cellStartTick + static_cast<int>(st) * subLen;
                                    harmNotes.push_back(SongNote(stTick, subLen - 2, midiKey, 1.0f, 0.0f, 0.0f, 0.5f));
                                    if (songHasLyrics) {
                                        const char* subLyric = (st == 0) ? lyricText : "-";
                                        pushVocalNote(stTick, subLen - 2, midiKey, subLyric);
                                    }
                                }
                            }
                        }
                    }
                }
                currentMatraGlobal++;
            }
        }

        if (songHasLyrics && m_precompiledVocalPlans.size() > 1) {
            const size_t nPlans = m_precompiledVocalPlans.size();
            for (size_t i = 0; i < nPlans; ++i) {
                const auto& nextPlan = m_precompiledVocalPlans[(i + 1) % nPlans];
                m_precompiledVocalPlans[i].nextLocusF1 = nextPlan.locusF1;
                m_precompiledVocalPlans[i].nextLocusF2 = nextPlan.locusF2;
                m_precompiledVocalPlans[i].nextLocusF3 = nextPlan.locusF3;
            }
        }

        m_hasSingingData.store(songHasLyrics);
        if (!songHasLyrics) {
            singNotes.shrink_to_fit();
            m_precompiledVocalPlans.shrink_to_fit();
            m_trackMuted[TRACK_SINGING] = true;
            m_singVoice.active = false;
        } else {
            m_trackMuted[TRACK_SINGING] = !m_userSingingEnabled.load();
        }

        int totalSongTicks = std::max(192, currentMatraGlobal * 48);
        int talaMatras = BuiltInPresets::parseTotalMatras(kt.divisions);
        m_drumLoopTicks.store(std::max(48, talaMatras * 48));
        m_totalLoopTicks = std::max(m_drumLoopTicks.load(), totalSongTicks);

        auto addHitByName = [&](const char* bolName, int tick, int len) {
            if (!bolName || !bolName[0] || std::strcmp(bolName, "-") == 0) return;
            int idx = BuiltInPresets::findTrackIndexCI(bolName);
            if (idx >= 0 && idx < NUM_TRACKS) {
                m_trackNotes[idx].push_back(SongNote(tick, len, 69, 1.0f));
            }
        };

        for (int m = 0; m < talaMatras; ++m) {
            int matraTick = m * 48;
            if (m < static_cast<int>(kt.bolCells.size())) {
                const char* bolCell = kt.bolCells[m];
                if (bolCell && std::strcmp(bolCell, "-") != 0) {
                    char buf[48];
                    std::strncpy(buf, bolCell, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    std::vector<const char*> subs;
                    char* tok = std::strtok(buf, " ");
                    while (tok) { subs.push_back(tok); tok = std::strtok(nullptr, " "); }
                    int subLen = 48 / std::max(1, static_cast<int>(subs.size()));
                    for (size_t sb = 0; sb < subs.size(); ++sb) {
                        addHitByName(subs[sb], matraTick + static_cast<int>(sb) * subLen, subLen);
                    }
                }
            }
            if (m < static_cast<int>(kt.kartalCells.size())) {
                const char* kCell = kt.kartalCells[m];
                if (kCell && std::strcmp(kCell, "Closed+Closed") == 0) {
                    addHitByName("Kartal Closed", matraTick, 24);
                    addHitByName("Kartal Closed", matraTick + 24, 24);
                } else if (kCell && std::strcmp(kCell, "-") != 0) {
                    addHitByName(kCell, matraTick, 48);
                }
            }
        }
        m_trackNotes[20].push_back(SongNote(0, 240, 60, 1.0f));

        m_songTickPos = 0.0f;
        m_lastTriggeredTick = -1;
    }

    void renderFramesMono(float* outMono, int frames) {
        if (m_droneEnabled.load() && m_droneDirty.load()) {
            rebakeDroneLoopInPsram();
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        const float dt = 1.0f / m_srate;
        const int bpmInt = m_bpm.load();
        const float currentBpm = static_cast<float>(bpmInt);
        const uint32_t lfoPhaseInc = static_cast<uint32_t>(bpmInt) * m_lfoBpmPhaseScale;
        const float beatDurationSec = 60.0f / currentBpm;
        const float ticksPerSec = (currentBpm / 60.0f) * 48.0f;
        const float secPerTick = 1.0f / ticksPerSec;
        const bool presetHarmOn = m_harmoniumPresetMode.load();
        const int drumLoop = std::max(48, m_drumLoopTicks.load());
        const int saOffset = m_saSemitone.load();

        // Snapshot bellows manual state once per block in Q14 (16384 = 1.0)
        const bool posMan = m_bellowsPosManual.load();
        const bool depMan = m_bellowsDepManual.load();
        const int32_t manWaveQ14 = static_cast<int32_t>((m_manualBellowsPos.load() * 2.0f - 1.0f) * 16384.0f);
        const int32_t manDepQ14  = depMan ? static_cast<int32_t>(m_manualBellowsDep.load() * 16384.0f) : 16384;
        const int32_t pressureGainQ14 = depMan ? (819 + ((15565 * manDepQ14) >> 14)) : 16384;

        for (int i = 0; i < frames; ++i) {
            int32_t fixedMixQ14 = 0; // Master fixed-point accumulator (16384 = 1.0)
            float floatMix = 0.0f;

            if (m_isPlaying.load() && !m_isPaused.load()) {
                int cdBeats = m_countdownBeats.load();
                if (cdBeats > 0) {
                    if (m_clickEnv > 0.001f) {
                        float clickFreq = (cdBeats == 1) ? 1200.0f : 880.0f;
                        floatMix += XpressiveParser::lut().sinRad(2.0f * M_PI_F * clickFreq * m_countdownTimerSec) * m_clickEnv * 0.4f;
                        m_clickEnv *= fastExpNeg(dt * 35.0f);
                    }
                    m_countdownTimerSec += dt;
                    if (m_countdownTimerSec >= beatDurationSec) {
                        m_countdownTimerSec -= beatDurationSec;
                        cdBeats--;
                        m_countdownBeats.store(cdBeats);
                        if (cdBeats > 0) m_clickEnv = 1.0f;
                    }
                } else {
                    int curTick = static_cast<int>(std::floor(m_songTickPos));
                    if (curTick != m_lastTriggeredTick) {
                        m_lastTriggeredTick = curTick;
                        int drumTick = curTick % drumLoop;
                        m_currentDrumTick.store(drumTick);

                        for (int tIdx = 0; tIdx < NUM_TRACKS; ++tIdx) {
                            if (m_trackMuted[tIdx]) continue;
                            if (tIdx == TRACK_HARMONIUM && !presetHarmOn) continue;

                            int matchTick = (tIdx < NUM_BAKED_DRUMS) ? drumTick : curTick;
                            for (const auto& sn : m_trackNotes[tIdx]) {
                                if (sn.startTick == matchTick) {
                                    if (tIdx < NUM_BAKED_DRUMS) {
                                        triggerBakedDrum(tIdx);
                                    } else if (tIdx == TRACK_HARMONIUM) {
                                        triggerHarmoniumPresetNote(sn.key + saOffset, sn.lenTicks * secPerTick);
                                        m_detectedMidiNote.store(sn.key + saOffset);
                                    } else if (tIdx == TRACK_SINGING) {
                                        int planIdx = static_cast<int>(sn.a3 + 0.5f);
                                        triggerSingingVoice(sn.key + saOffset, sn.lenTicks * secPerTick, true, planIdx);
                                        if (!presetHarmOn || m_trackMuted[TRACK_HARMONIUM]) {
                                            m_detectedMidiNote.store(sn.key + saOffset);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    m_songTickPos += ticksPerSec * dt;
                    if (m_songTickPos >= m_totalLoopTicks) {
                        m_songTickPos = std::fmod(m_songTickPos, static_cast<float>(m_totalLoopTicks));
                        m_lastTriggeredTick = -1;
                    }
                }
            }

          
            for (auto& dv : m_drumVoices) {
                if (!dv.active) continue;
                if (m_trackMuted[dv.trackIdx]) { dv.active = false; continue; }
                const int16_t* pcm = m_bakedDrums[dv.trackIdx];
                if (pcm && dv.samplePos < m_bakedDrumLen) {
                    fixedMixQ14 += (static_cast<int32_t>(pcm[dv.samplePos++]) >> 1);
                } else {
                    dv.active = false;
                }
            }

          
            if (m_droneEnabled.load() && m_droneLoopBuf && m_droneLoopLen > 0) {
                fixedMixQ14 += (static_cast<int32_t>(m_droneLoopBuf[m_droneReadIdx]) >> 1);
                if (++m_droneReadIdx >= m_droneLoopLen) m_droneReadIdx = 0;
            }

          
            if (!m_trackMuted[TRACK_HARMONIUM]) {
                int32_t harmMixQ14 = 0;
                for (auto& hv : m_harmVoices) {
                    if (!hv.active) continue;
                    if (hv.isAutoPreset && (!m_isPlaying.load() || !presetHarmOn)) {
                        hv.active = false;
                        continue;
                    }
                    if (hv.isAutoPreset && hv.held && hv.samplesElapsed >= hv.autoDurSamples) {
                        hv.held = false;
                    }
                    if (!hv.held) {
                        hv.relEnvQ14 = (hv.relEnvQ14 * m_relDecayQ15) >> 15;
                        if (hv.relEnvQ14 < 18) { // Below exp(-18 * 0.38s) threshold
                            hv.active = false;
                            continue;
                        }
                    }
                    harmMixQ14 += renderFastHarmoniumSampleQ14(hv, lfoPhaseInc, posMan, manWaveQ14, manDepQ14, pressureGainQ14);
                    hv.samplesElapsed++;
                }

                harmMixQ14 = m_liveHarmoniumTf.processQ14(harmMixQ14);
                // Scale by 0.22f (3604 in Q14)
                fixedMixQ14 += (harmMixQ14 * 3604) >> 14;
            }

          
            if (m_singVoice.active && !m_trackMuted[TRACK_SINGING]) {
                if (m_singVoice.isAutoPreset && !m_isPlaying.load()) {
                    m_singVoice.active = false;
                } else {
                    if (m_singVoice.isAutoPreset && m_singVoice.held && m_singVoice.t >= m_singVoice.autoDurSec) {
                        m_singVoice.held = false;
                        m_singVoice.trel = 0.0f;
                    }
                    if (!m_singVoice.held) {
                        m_singVoice.trel += dt;
                        if (m_singVoice.trel > 0.22f) {
                            m_singVoice.active = false;
                        }
                    }
                    if (m_singVoice.active) {
                        float dryVocal = renderPrecompiledSingingSample(dt);
                        floatMix += processVocalChamber(dryVocal) * 0.44f;
                        m_singVoice.t += dt;
                    }
                }
            }

            float totalMix = static_cast<float>(fixedMixQ14) * (1.0f / 16384.0f) + floatMix;
            outMono[i] = fastTanh(totalMix);
        }
    }

    void noteOn(int midiNote, int velocity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int shifted = my_clamp(midiNote + m_octaveShift.load() * 12, 12, 115);
        m_detectedMidiNote.store(shifted);

        if (!m_trackMuted[TRACK_HARMONIUM]) {
            int32_t velQ14 = static_cast<int32_t>(my_clamp(velocity / 127.0f, 0.15f, 1.0f) * 16384.0f);
            for (auto& hv : m_harmVoices) {
                if (hv.active && hv.held && !hv.isAutoPreset && (hv.midiNote == shifted || hv.rawMidiNote == midiNote)) {
                    hv.midiNote = shifted;
                    hv.rawMidiNote = midiNote;
                    hv.velQ14 = velQ14;
                    return;
                }
            }
            HarmoniumVoice* slot = &m_harmVoices[0];
            for (auto& hv : m_harmVoices) {
                if (!hv.active) { slot = &hv; break; }
            }
            slot->active = true;
            slot->held = true;
            slot->isAutoPreset = false;
            slot->midiNote = shifted;
            slot->rawMidiNote = midiNote;
            slot->velQ14 = velQ14;
            slot->attackEnvQ14 = 0;
            slot->relEnvQ14 = 16384;
            slot->samplesElapsed = 0;
            slot->phaseLfo = 0;
            slot->phaseVib = 0;
            slot->phaseBase = 0;
            slot->phaseCeleste = 0;
        }
    }

    void noteOff(int midiNote) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int shifted = my_clamp(midiNote + m_octaveShift.load() * 12, 12, 115);
        for (auto& hv : m_harmVoices) {
            if (hv.active && hv.held && !hv.isAutoPreset && (hv.rawMidiNote == midiNote || hv.midiNote == shifted)) {
                hv.held = false;
            }
        }
    }

    void startWithCountdown(bool cd) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_songTickPos = 0.0f;
        m_lastTriggeredTick = -1;
        m_isPaused.store(false);
        for (auto& dv : m_drumVoices) dv.active = false;
        for (auto& hv : m_harmVoices) if (hv.isAutoPreset) hv.active = false;
        if (m_singVoice.isAutoPreset) m_singVoice.active = false;
        m_countdownBeats.store(cd ? 4 : 0);
        m_countdownTimerSec = 0.0f;
        m_clickEnv = 1.0f;
        m_isPlaying.store(true);
    }

    void stopPlayback() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isPlaying.store(false);
        m_isPaused.store(false);
        m_countdownBeats.store(0);
        m_songTickPos = 0.0f;
        m_lastTriggeredTick = -1;
        for (auto& dv : m_drumVoices) dv.active = false;
        for (auto& hv : m_harmVoices) if (hv.isAutoPreset) hv.active = false;
        if (m_singVoice.isAutoPreset) m_singVoice.active = false;
    }

    void togglePause() {
        if (m_isPlaying.load()) m_isPaused.store(!m_isPaused.load());
    }

    void setBpm(int bpm) {
        m_bpm.store(my_clamp(bpm, 40, 260));
        m_droneDirty.store(true);
    }
    int getBpm() const { return m_bpm.load(); }

    void setSaSemitone(int sa) {
        m_saSemitone.store((sa % 12 + 12) % 12);
        m_droneDirty.store(true);
    }
    int getSaSemitone() const { return m_saSemitone.load(); }

    void setDroneEnabled(bool en) {
        if (en) m_droneDirty.store(true);
        m_droneEnabled.store(en);
    }
    bool isDroneEnabled() const { return m_droneEnabled.load(); }

    void setDronePreset(int p) {
        m_dronePreset.store(p % 3);
        m_droneDirty.store(true);
    }
    int getDronePreset() const { return m_dronePreset.load(); }

    void setDroneSwaraMode(int m) {
        m_droneSwaraMode.store(m % 4);
        m_droneDirty.store(true);
    }
    int getDroneSwaraMode() const { return m_droneSwaraMode.load(); }

    void toggleTrackMuted(int idx) {
        if (idx >= 0 && idx < NUM_TRACKS) {
            m_trackMuted[idx] = !m_trackMuted[idx];
            if (idx == TRACK_SINGING) {
                m_userSingingEnabled.store(!m_trackMuted[TRACK_SINGING]);
                if (m_trackMuted[TRACK_SINGING]) m_singVoice.active = false;
            }
        }
    }
    bool isTrackMuted(int idx) const {
        return (idx >= 0 && idx < NUM_TRACKS) ? m_trackMuted[idx] : false;
    }

    bool isHarmoniumEnabled() const { return !m_trackMuted[TRACK_HARMONIUM]; }
    void setHarmoniumEnabled(bool en) {
        m_trackMuted[TRACK_HARMONIUM] = !en;
        if (!en) {
            for (auto& hv : m_harmVoices) hv.active = false;
        }
    }
    void toggleHarmoniumEnabled() { setHarmoniumEnabled(!isHarmoniumEnabled()); }

    bool isSingingEnabled() const { return !m_trackMuted[TRACK_SINGING]; }
    void setSingingEnabled(bool en) {
        m_userSingingEnabled.store(en);
        m_trackMuted[TRACK_SINGING] = !(en && m_hasSingingData.load());
        if (!en) m_singVoice.active = false;
    }
    void toggleSingingEnabled() { setSingingEnabled(!isSingingEnabled()); }

    bool hasSingingData() const { return m_hasSingingData.load(); }

    void setMidiInMode(int m) { m_midiInMode.store(m % 4); }
    int getMidiInMode() const { return m_midiInMode.load(); }

    void setHarmoniumPresetMode(bool en) { m_harmoniumPresetMode.store(en); }
    bool isHarmoniumPresetMode() const { return m_harmoniumPresetMode.load(); }

    void setOctaveShift(int oct) { m_octaveShift.store(my_clamp(oct, -2, 2)); }
    int getOctaveShift() const { return m_octaveShift.load(); }

    void setTfEnabled(bool en) { m_liveHarmoniumTf.enabled = en; }
    bool isTfEnabled() const { return m_liveHarmoniumTf.enabled; }

    void setBellowsPosManual(bool man) { m_bellowsPosManual.store(man); }
    bool isBellowsPosManual() const { return m_bellowsPosManual.load(); }

    void setBellowsDepManual(bool man) { m_bellowsDepManual.store(man); }
    bool isBellowsDepManual() const { return m_bellowsDepManual.load(); }

    void updateBellowsSensors(float posNorm01, float depNorm01) {
        m_manualBellowsPos.store(my_clamp(posNorm01, 0.0f, 1.0f));
        m_manualBellowsDep.store(my_clamp(depNorm01, 0.0f, 1.5f));
    }

    bool isPlaying() const { return m_isPlaying.load(); }
    bool isPaused() const { return m_isPaused.load(); }
    int getSelectedTala() const { return m_currentTalaIdx.load(); }
    int getSelectedSong() const { return m_currentHarmSongIdx.load(); }
    int getDetectedMidiNote() const { return m_detectedMidiNote.load(); }
    int getCountdownBeats() const { return m_countdownBeats.load(); }
    int getDrumLoopMatras() const { return m_drumLoopTicks.load() / 48; }
    int getCurrentMatra() const { return (m_currentDrumTick.load() / 48) + 1; }

private:
    struct PrecompiledFormantSlice {
        float f1, f2, f3;
        float alpha1, alpha2, alpha3;
        float g1, g2, g3;
        float b1, b2, b3;
    };

    struct KlattResonator {
        float a = 1.0f, b = 0.0f, c = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
        void setParams(float freqHz, float bwHz, float srate) {
            float f = my_clamp(freqHz, 120.0f, srate * 0.44f);
            float bw = my_clamp(bwHz, 40.0f, 500.0f);
            float r = std::exp(-M_PI_F * bw / srate);
            c = -(r * r);
            b = 2.0f * r * XpressiveParser::lut().cosRad(2.0f * M_PI_F * f / srate);
            a = 1.0f - b - c;
        }
        inline float process(float x) {
            float y = a * x + b * y1 + c * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
        void reset() { y1 = 0.0f; y2 = 0.0f; }
    };

    struct FricativeBandpassSVF {
        float low = 0.0f, band = 0.0f;
        inline float process(float input, float cutoffHz, float qDamper, float srate) {
            float f = 2.0f * XpressiveParser::lut().sinUnit(M_PI_F * my_clamp(cutoffHz, 300.0f, srate * 0.42f) / srate);
            low += f * band;
            float high = input - low - qDamper * band;
            band += f * high;
            return band;
        }
        void reset() { low = 0.0f; band = 0.0f; }
    };

    struct VocalSyllablePlan {
        float onsetGapSec   = 0.0f;
        float onsetDurSec   = 0.045f;
        float onsetA1       = 0.0f;
        float glideA1       = -1.0f;
        float vowelA1       = 0.0f;
        float tractScaleA3  = 0.50f;
        float noiseAmp      = 0.15f;
        float noiseDecay    = 55.0f;
        float noiseFreq     = 2800.0f;
        float aspiration    = 0.08f;
        bool  hasNasalOnset = false;
        bool  hasVoiceBar   = false;
        bool  hasTrillOnset = false;
        uint8_t codaType    = 0;
        bool  isLegatoMelisma = false;
        float preFricDurSec   = 0.0f;
        float preFricFreq     = 4400.0f;
        float preFricAmp      = 0.0f;
        float stopGapSec      = 0.0f;
        float burstDurSec     = 0.018f;
        float burstFreq       = 2500.0f;
        float burstBwQ        = 0.35f;
        float burstAmp        = 0.12f;
        float transDurSec     = 0.048f;
        float locusF1         = 500.0f;
        float locusF2         = 1500.0f;
        float locusF3         = 2500.0f;
        bool  hasGlide        = false;
        float glideF1         = 350.0f;
        float glideF2         = 2000.0f;
        float glideF3         = 2600.0f;
        float vowelF1         = 800.0f;
        float vowelF2         = 1200.0f;
        float vowelF3         = 2500.0f;
        float vowelB1         = 80.0f;
        float vowelB2         = 90.0f;
        float vowelB3         = 120.0f;
        float nextLocusF1     = 500.0f;
        float nextLocusF2     = 1500.0f;
        float nextLocusF3     = 2500.0f;
    };

    struct BakedDrumVoice {
        bool active = false;
        int trackIdx = 0;
        int samplePos = 0;
    };

   
    struct HarmoniumVoice {
        bool active = false;
        bool held = false;
        bool isAutoPreset = false;
        int midiNote = 60;
        int rawMidiNote = 60;
        int32_t velQ14 = 16384;       // Q14 velocity (16384 = 1.0)
        int32_t attackEnvQ14 = 0;     // Q14 1-pole attack envelope
        int32_t relEnvQ14 = 16384;    // Q14 1-pole release envelope
        int32_t samplesElapsed = 0;
        int32_t autoDurSamples = 11025;
        uint32_t phaseLfo = 0;        // Q32 phase accumulator for BPM bellows LFO
        uint32_t phaseVib = 0;        // Q32 phase accumulator for vibrato fundamental
        uint32_t phaseBase = 0;       // Q32 phase accumulator for principal + sub-octave
        uint32_t phaseCeleste = 0;    // Q32 phase accumulator for +8 cent celeste reed
    };

    struct SingingVoice {
        bool active = false;
        bool held = false;
        bool isAutoPreset = false;
        int midiNote = 60;
        float t = 0.0f;
        float trel = 0.0f;
        float autoDurSec = 0.5f;
        float curFreq = 261.63f;
        float targetFreq = 261.63f;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float curA1 = 0.0f;
        float curA3 = 0.5f;

        float curF1 = 800.0f, curF2 = 1200.0f, curF3 = 2500.0f;
        float curB1 = 80.0f, curB2 = 90.0f, curB3 = 120.0f;
        float cycleShimmer = 1.0f;
        uint32_t rngState = 0x13579BDFu;
        int filterSubSample = 0;
        KlattResonator res1, res2, res3, res4, resNasal;
        FricativeBandpassSVF fricSvf, breathSvf;

        VocalSyllablePlan plan;
    };

    void initPrecompiledFormantLut() {
        const float B1_tab[5] = {80.0f, 70.0f, 60.0f, 70.0f, 60.0f};
        const float B2_tab[5] = {90.0f, 100.0f, 100.0f, 80.0f, 80.0f};
        const float B3_tab[5] = {120.0f, 120.0f, 150.0f, 110.0f, 100.0f};
        const float G1_tab[5] = {1.00f, 1.00f, 1.00f, 1.00f, 1.00f};
        const float G2_tab[5] = {0.50f, 0.45f, 0.35f, 0.45f, 0.35f};
        const float G3_tab[5] = {0.25f, 0.25f, 0.20f, 0.20f, 0.15f};

        for (int i = 0; i < 256; ++i) {
            float a = static_cast<float>(i) / 255.0f;
            float a2 = a * a;
            float a3 = a2 * a;
            float a4 = a3 * a;

            float f1 = -14933.33f * a4 + 27520.0f * a3 - 14026.67f * a2 + 940.0f * a + 800.0f;
            float f2 =  51626.67f * a4 - 95360.0f * a3 + 47493.33f * a2 - 4160.0f * a + 1200.0f;
            float f3 =  33066.67f * a4 - 66666.67f * a3 + 39533.33f * a2 - 6233.33f * a + 2500.0f;

            float seg = a * 4.0f;
            int idx0 = std::min(3, static_cast<int>(std::floor(seg)));
            float frac = seg - static_cast<float>(idx0);

            float b1 = B1_tab[idx0] + frac * (B1_tab[idx0 + 1] - B1_tab[idx0]);
            float b2 = B2_tab[idx0] + frac * (B2_tab[idx0 + 1] - B2_tab[idx0]);
            float b3 = B3_tab[idx0] + frac * (B3_tab[idx0 + 1] - B3_tab[idx0]);

            m_formantLut[i].f1 = my_clamp(f1, 240.0f, 900.0f);
            m_formantLut[i].f2 = my_clamp(f2, 750.0f, 2450.0f);
            m_formantLut[i].f3 = my_clamp(f3, 2100.0f, 3150.0f);
            m_formantLut[i].b1 = b1;
            m_formantLut[i].b2 = b2;
            m_formantLut[i].b3 = b3;
            m_formantLut[i].alpha1 = M_PI_F * b1;
            m_formantLut[i].alpha2 = M_PI_F * b2;
            m_formantLut[i].alpha3 = M_PI_F * b3;
            m_formantLut[i].g1 = G1_tab[idx0] + frac * (G1_tab[idx0 + 1] - G1_tab[idx0]);
            m_formantLut[i].g2 = G2_tab[idx0] + frac * (G2_tab[idx0 + 1] - G2_tab[idx0]);
            m_formantLut[i].g3 = G3_tab[idx0] + frac * (G3_tab[idx0 + 1] - G3_tab[idx0]);
        }
    }

    void initVocalChamber() {
        std::memset(m_vocalChamberBuf, 0, sizeof(m_vocalChamberBuf));
        m_vocalChamberIdx = 0;
        m_tap1 = std::max(1, static_cast<int>(0.013f * m_srate));
        m_tap2 = std::max(1, static_cast<int>(0.029f * m_srate));
        m_tap3 = std::max(1, static_cast<int>(0.047f * m_srate));
    }

    inline float processVocalChamber(float x) {
        int r1 = (m_vocalChamberIdx + VOCAL_CHAMBER_LEN - m_tap1) & (VOCAL_CHAMBER_LEN - 1);
        int r2 = (m_vocalChamberIdx + VOCAL_CHAMBER_LEN - m_tap2) & (VOCAL_CHAMBER_LEN - 1);
        int r3 = (m_vocalChamberIdx + VOCAL_CHAMBER_LEN - m_tap3) & (VOCAL_CHAMBER_LEN - 1);
        float early = 0.20f * m_vocalChamberBuf[r1] - 0.14f * m_vocalChamberBuf[r2] + 0.10f * m_vocalChamberBuf[r3];
        m_vocalChamberBuf[m_vocalChamberIdx] = x + early * 0.28f;
        m_vocalChamberIdx = (m_vocalChamberIdx + 1) & (VOCAL_CHAMBER_LEN - 1);
        return x + early;
    }

    static VocalSyllablePlan makeLegatoContinuation(const VocalSyllablePlan& prev) {
        VocalSyllablePlan plan = prev;
        plan.isLegatoMelisma = true;
        plan.preFricDurSec   = 0.0f;
        plan.preFricAmp      = 0.0f;
        plan.stopGapSec      = 0.0f;
        plan.burstAmp        = 0.0f;
        plan.hasGlide        = false;
        plan.transDurSec     = 0.020f;
        plan.locusF1         = prev.vowelF1;
        plan.locusF2         = prev.vowelF2;
        plan.locusF3         = prev.vowelF3;
        plan.onsetGapSec     = 0.0f;
        plan.noiseAmp        = 0.0f;
        plan.hasNasalOnset   = false;
        plan.hasVoiceBar     = false;
        plan.hasTrillOnset   = false;
        return plan;
    }

    VocalSyllablePlan compileSanskritSyllable(const char* syl) const {
        VocalSyllablePlan plan;
        if (!syl || !*syl || std::strcmp(syl, "-") == 0 || std::strcmp(syl, ".") == 0) {
            plan.noiseAmp = 0.0f;
            plan.burstAmp = 0.0f;
            return plan;
        }

        char s[32];
        std::strncpy(s, syl, 31);
        s[31] = '\0';
        for (int i = 0; s[i]; ++i) s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        std::string str(s);

        if (str.find("u") != std::string::npos || str.find("oo") != std::string::npos) {
            plan.vowelA1 = 1.00f;
        } else if (str.find("o") != std::string::npos || str.find("au") != std::string::npos) {
            plan.vowelA1 = 0.75f;
        } else if (str.find("e") != std::string::npos || str.find("ai") != std::string::npos) {
            plan.vowelA1 = 0.25f;
        } else if (str.find("i") != std::string::npos || str.find("ee") != std::string::npos) {
            plan.vowelA1 = 0.50f;
        } else {
            plan.vowelA1 = 0.00f;
        }

        int vIdx = my_clamp(static_cast<int>(plan.vowelA1 * 255.0f + 0.5f), 0, 255);
        plan.vowelF1 = m_formantLut[vIdx].f1;
        plan.vowelF2 = m_formantLut[vIdx].f2;
        plan.vowelF3 = m_formantLut[vIdx].f3;
        plan.vowelB1 = m_formantLut[vIdx].b1;
        plan.vowelB2 = m_formantLut[vIdx].b2;
        plan.vowelB3 = m_formantLut[vIdx].b3;

        plan.locusF1 = plan.vowelF1;
        plan.locusF2 = plan.vowelF2;
        plan.locusF3 = plan.vowelF3;

        int len = static_cast<int>(str.length());
        char lastC = str.back();
        if (lastC == 'm' || lastC == 'n') {
            plan.codaType = 1;
        } else if (lastC == 'r') {
            plan.codaType = 2;
        } else if (lastC == 'h') {
            plan.codaType = 3;
        } else if (lastC == 'd' || lastC == 'j' || lastC == 't' || lastC == 'k') {
            plan.codaType = 4;
        }

        if (str.find("h") != std::string::npos) {
            plan.aspiration = 0.24f;
        }

        if (str.find("r") != std::string::npos && lastC != 'r') {
            plan.hasTrillOnset = true;
            plan.glideA1 = 0.20f;
            plan.hasGlide = true;
            plan.glideF1 = 420.0f;
            plan.glideF2 = 1350.0f;
            plan.glideF3 = 1780.0f;
            plan.transDurSec = 0.062f;
        } else if (str.find("y") != std::string::npos) {
            plan.onsetA1 = 0.50f;
            plan.glideA1 = 0.50f;
            plan.onsetDurSec = 0.055f;
            plan.hasGlide = true;
            plan.glideF1 = 280.0f;
            plan.glideF2 = 2280.0f;
            plan.glideF3 = 2950.0f;
            plan.transDurSec = 0.060f;
        } else if (str.find("v") == 0) {
            plan.onsetA1 = 0.95f;
            plan.glideA1 = 0.90f;
            plan.onsetDurSec = 0.050f;
        } else if (str.find("l") != std::string::npos) {
            plan.onsetA1 = 0.32f;
            plan.glideA1 = 0.30f;
            plan.onsetDurSec = 0.045f;
            plan.hasGlide = true;
            plan.glideF1 = 360.0f;
            plan.glideF2 = 1520.0f;
            plan.glideF3 = 2580.0f;
            plan.transDurSec = 0.055f;
        } else {
            plan.onsetA1 = plan.vowelA1;
        }

        if (str.rfind("ste", 0) == 0 || str.rfind("st", 0) == 0 || str.rfind("s", 0) == 0) {
            plan.onsetGapSec  = (str.rfind("st", 0) == 0) ? 0.012f : 0.0f;
            plan.onsetDurSec  = 0.050f;
            plan.onsetA1      = 0.35f;
            plan.tractScaleA3 = (str.rfind("st", 0) == 0) ? 0.62f : 0.64f;
            plan.noiseAmp     = 0.88f;
            plan.noiseDecay   = 34.0f;
            plan.noiseFreq    = 4500.0f;
            plan.preFricDurSec = 0.040f;
            plan.preFricFreq   = 4650.0f;
            plan.preFricAmp    = 0.88f;
            plan.burstFreq     = (str.rfind("st", 0) == 0) ? 3400.0f : 4650.0f;
            plan.burstBwQ      = 0.20f;
            plan.burstAmp      = (str.rfind("st", 0) == 0) ? 0.72f : 0.45f;
            plan.locusF1       = 260.0f;
            plan.locusF2       = 1780.0f;
            plan.locusF3       = 2650.0f;
        } else if (str.rfind("ksh", 0) == 0 || (str.rfind("k", 0) == 0)) {
            plan.onsetGapSec  = 0.016f;
            plan.onsetDurSec  = 0.042f;
            plan.onsetA1      = 0.22f;
            plan.tractScaleA3 = 0.60f;
            plan.noiseAmp     = 0.78f;
            plan.noiseDecay   = 52.0f;
            plan.noiseFreq    = 1850.0f;
            plan.stopGapSec    = 0.015f;
            plan.burstDurSec   = (str.rfind("ksh", 0) == 0) ? 0.048f : 0.020f;
            plan.burstFreq     = (str.rfind("ksh", 0) == 0) ? 2750.0f : ((plan.vowelF2 > 1500.0f) ? 2150.0f : 1450.0f);
            plan.burstBwQ      = 0.25f;
            plan.burstAmp      = 0.80f;
            plan.locusF1       = 290.0f;
            plan.locusF2       = (plan.vowelF2 > 1500.0f) ? 2100.0f : 1380.0f;
            plan.locusF3       = 2300.0f;
        } else if (str.rfind("sh", 0) == 0) {
            plan.onsetGapSec  = 0.0f;
            plan.onsetDurSec  = 0.055f;
            plan.onsetA1      = 0.45f;
            plan.tractScaleA3 = 0.60f;
            plan.noiseAmp     = 0.85f;
            plan.noiseDecay   = 32.0f;
            plan.noiseFreq    = 2850.0f;
            plan.preFricDurSec = 0.042f;
            plan.preFricFreq   = 2650.0f;
            plan.preFricAmp    = 0.85f;
            plan.burstFreq     = 2650.0f;
            plan.burstBwQ      = 0.22f;
            plan.burstAmp      = 0.50f;
            plan.locusF1       = 280.0f;
            plan.locusF2       = 2150.0f;
            plan.locusF3       = 2750.0f;
        } else if (str.rfind("h", 0) == 0) {
            plan.onsetGapSec  = 0.0f;
            plan.onsetDurSec  = 0.045f;
            plan.onsetA1      = plan.vowelA1;
            plan.tractScaleA3 = 0.52f;
            plan.noiseAmp     = 0.55f;
            plan.noiseDecay   = 36.0f;
            plan.noiseFreq    = 1650.0f;
            plan.aspiration   = 0.32f;
            plan.preFricDurSec = 0.028f;
            plan.preFricFreq   = plan.vowelF2;
            plan.preFricAmp    = 0.48f;
            plan.burstFreq     = plan.vowelF2;
            plan.burstAmp      = 0.35f;
        } else if (str.rfind("t", 0) == 0) {
            plan.onsetGapSec  = 0.016f;
            plan.onsetDurSec  = 0.038f;
            plan.onsetA1      = 0.35f;
            plan.tractScaleA3 = 0.58f;
            plan.noiseAmp     = 0.75f;
            plan.noiseDecay   = 60.0f;
            plan.noiseFreq    = 3300.0f;
            plan.stopGapSec    = 0.017f;
            plan.burstDurSec   = 0.016f;
            plan.burstFreq     = 3450.0f;
            plan.burstBwQ      = 0.28f;
            plan.burstAmp      = 0.78f;
            plan.locusF1       = 250.0f;
            plan.locusF2       = 1760.0f;
            plan.locusF3       = 2620.0f;
        } else if (str.rfind("p", 0) == 0) {
            plan.onsetGapSec  = 0.015f;
            plan.onsetDurSec  = 0.042f;
            plan.onsetA1      = 0.85f;
            plan.tractScaleA3 = 0.42f;
            plan.noiseAmp     = 0.68f;
            plan.noiseDecay   = 58.0f;
            plan.noiseFreq    = 1100.0f;
            plan.stopGapSec    = 0.016f;
            plan.burstDurSec   = 0.018f;
            plan.burstFreq     = 950.0f;
            plan.burstBwQ      = 0.25f;
            plan.burstAmp      = 0.65f;
            plan.locusF1       = 280.0f;
            plan.locusF2       = 1100.0f;
            plan.locusF3       = 2200.0f;
        } else if (str.rfind("d", 0) == 0 || str.rfind("g", 0) == 0 || str.rfind("b", 0) == 0 || str.rfind("j", 0) == 0) {
            plan.onsetGapSec  = 0.012f;
            plan.onsetDurSec  = 0.042f;
            plan.hasVoiceBar  = true;
            plan.onsetA1      = (s[0] == 'b') ? 0.85f : (s[0] == 'j' ? 0.50f : 0.35f);
            plan.tractScaleA3 = (s[0] == 'b') ? 0.42f : 0.50f;
            plan.noiseAmp     = (s[0] == 'j') ? 0.58f : 0.42f;
            plan.noiseDecay   = 55.0f;
            plan.noiseFreq    = (s[0] == 'j') ? 2600.0f : (s[0] == 'b' ? 1050.0f : 2200.0f);
        } else if (str.rfind("n", 0) == 0 || str.rfind("m", 0) == 0) {
            plan.onsetGapSec  = 0.0f;
            plan.onsetDurSec  = 0.048f;
            plan.hasNasalOnset = true;
            plan.onsetA1      = (s[0] == 'm') ? 0.85f : 0.40f;
            plan.tractScaleA3 = 0.38f;
            plan.noiseAmp     = 0.08f;
            plan.noiseDecay   = 70.0f;
            plan.noiseFreq    = 1200.0f;
        } else if (str.rfind("r", 0) == 0) {
            plan.onsetGapSec  = 0.0f;
            plan.onsetDurSec  = 0.045f;
            plan.hasTrillOnset = true;
            plan.onsetA1      = 0.25f;
            plan.tractScaleA3 = 0.45f;
            plan.noiseAmp     = 0.22f;
            plan.noiseDecay   = 50.0f;
            plan.noiseFreq    = 1900.0f;
        } else {
            plan.onsetGapSec  = 0.0f;
            plan.onsetDurSec  = 0.025f;
            plan.noiseAmp     = 0.05f;
            plan.tractScaleA3 = 0.50f;
        }
        (void)len;
        return plan;
    }

    float renderPrecompiledSingingSample(float dt) {
        const auto& lut = XpressiveParser::lut();
        const VocalSyllablePlan& plan = m_singVoice.plan;
        const float t = m_singVoice.t;
        const float dur = std::max(0.12f, m_singVoice.autoDurSec);

        m_singVoice.curFreq += (m_singVoice.targetFreq - m_singVoice.curFreq) * std::min(1.0f, dt * 65.0f);
        float vibEnv = 1.0f - fastExpNeg(3.5f * t);
        float vib1 = 0.011f * vibEnv * lut.sinUnit(5.5f * t);
        float vib2 = 0.011f * vibEnv * lut.sinUnit(5.8f * t + 0.17f);
        float pitchScoop = -0.022f * fastExpNeg(28.0f * t);

        float f0_1 = m_singVoice.curFreq * (1.0f + pitchScoop + vib1);
        float f0_2 = m_singVoice.curFreq * 1.0025f * (1.0f + pitchScoop + vib2);

        float targetA1 = plan.vowelA1;
        float targetA3 = plan.tractScaleA3;
        float nasalMix = 0.0f;
        float trillMod = 1.0f;
        float codaBreath = 0.0f;
        float f3RetroflexPull = 1.0f;

        const float codaStart = std::max(plan.onsetGapSec + plan.onsetDurSec + 0.03f,
                                         dur - std::min(0.14f, dur * 0.30f));

        if (t < plan.onsetGapSec + plan.onsetDurSec) {
            float u = (t - plan.onsetGapSec) / std::max(0.015f, plan.onsetDurSec);
            u = my_clamp(u, 0.0f, 1.0f);
            float sCurve = u * u * (3.0f - 2.0f * u);
            float startVowel = (plan.glideA1 >= 0.0f) ? plan.glideA1 : plan.onsetA1;
            targetA1 = startVowel + sCurve * (plan.vowelA1 - startVowel);

            if (plan.hasNasalOnset) {
                nasalMix = 1.0f - sCurve;
            }
            if (plan.hasTrillOnset) {
                trillMod = 0.55f + 0.45f * lut.sinUnit(28.0f * t);
                f3RetroflexPull = 0.72f + 0.28f * sCurve;
            }
        } else if (plan.codaType != 0 && t > codaStart) {
            float u = my_clamp((t - codaStart) / std::max(0.02f, dur - codaStart), 0.0f, 1.0f);
            float sCurve = u * u * (3.0f - 2.0f * u);
            if (plan.codaType == 1) {
                nasalMix = sCurve * 0.85f;
                targetA1 = plan.vowelA1 + sCurve * (0.85f - plan.vowelA1);
                targetA3 = plan.tractScaleA3 * (1.0f - 0.35f * sCurve);
            } else if (plan.codaType == 2) {
                f3RetroflexPull = 1.0f - 0.30f * sCurve;
                trillMod = 1.0f - 0.30f * sCurve * (0.5f + 0.5f * lut.sinUnit(26.0f * t));
            } else if (plan.codaType == 3) {
                codaBreath = sCurve * 0.55f;
            } else if (plan.codaType == 4) {
                targetA1 = plan.vowelA1 + sCurve * (0.50f - plan.vowelA1);
                trillMod = 1.0f - 0.75f * sCurve;
            }
        }

        float smoothAlpha = std::min(1.0f, dt / 0.012f);
        m_singVoice.curA1 += (targetA1 - m_singVoice.curA1) * smoothAlpha;
        m_singVoice.curA3 += (targetA3 - m_singVoice.curA3) * smoothAlpha;

        int lutIdx = my_clamp(static_cast<int>(m_singVoice.curA1 * 255.0f + 0.5f), 0, 255);
        const PrecompiledFormantSlice& slice = m_formantLut[lutIdx];

        float scale1 = 0.85f + 0.30f * m_singVoice.curA3;
        float scale2 = 0.86f + 0.30f * m_singVoice.curA3;

        float F1 = slice.f1 * scale1;
        float F2 = slice.f2 * scale1;
        float F3 = slice.f3 * scale1 * f3RetroflexPull;

        m_singVoice.phase1 += f0_1 * dt;
        m_singVoice.phase1 -= std::floor(m_singVoice.phase1);
        m_singVoice.phase2 += f0_2 * dt;
        m_singVoice.phase2 -= std::floor(m_singVoice.phase2);

        float phi1 = m_singVoice.phase1;
        float tau1 = phi1 / f0_1;
        float phi2 = m_singVoice.phase2;
        float tau2 = phi2 / f0_2;

        float oneMinusPhi1 = 1.0f - phi1;
        float fofWin1 = (1.0f - fastExpNeg(140.0f * phi1)) * (oneMinusPhi1 * oneMinusPhi1);

        float env1 = slice.g1 * fastExpNeg(slice.alpha1 * tau1);
        float env2 = slice.g2 * fastExpNeg(slice.alpha2 * tau1);
        float env3 = slice.g3 * fastExpNeg(slice.alpha3 * tau1);

        float fof1 = env1 * lut.sinUnit(F1 * tau1)
                     + env2 * lut.sinUnit(F2 * tau1)
                     + env3 * lut.sinUnit(F3 * tau1)
                     + 0.12f * fastExpNeg(420.0f * tau1) * lut.sinUnit(3150.0f * tau1);

        float n1 = std::max(1.0f, std::floor(F1 / f0_1 + 0.5f));
        float n2 = std::max(1.0f, std::floor(F2 / f0_1 + 0.5f));
        float n3 = std::max(1.0f, std::floor(F3 / f0_1 + 0.5f));
        float samLayer = lut.sinUnit(0.5f * phi1) * (
                             env1 * lut.sinUnit(n1 * phi1) +
                             env2 * lut.sinUnit(n2 * phi1) +
                             0.6f * env3 * lut.sinUnit(n3 * phi1)
                             );

        float oneMinusPhi2 = 1.0f - phi2;
        float fofWin2 = (1.0f - fastExpNeg(140.0f * phi2)) * (oneMinusPhi2 * oneMinusPhi2);
        float fof2 = slice.g1 * fastExpNeg(slice.alpha1 * tau2) * lut.sinUnit(slice.f1 * scale2 * tau2)
                     + slice.g2 * fastExpNeg(slice.alpha2 * tau2) * lut.sinUnit(slice.f2 * scale2 * tau2)
                     + slice.g3 * fastExpNeg(slice.alpha3 * tau2) * lut.sinUnit(slice.f3 * scale2 * f3RetroflexPull * tau2);

        float chestFund = 0.28f * lut.sinUnit(phi1) * (oneMinusPhi1 * oneMinusPhi1);
        float nasalWave = (0.55f * lut.sinUnit(phi1) + 0.25f * fastExpNeg(180.0f * tau1) * lut.sinUnit(260.0f * tau1)
                           + 0.12f * fastExpNeg(400.0f * tau1) * lut.sinUnit(1000.0f * tau1)) * fofWin1;

        float voicedCore = (0.58f * fofWin1 * fof1 + 0.22f * fofWin2 * fof2 + 0.20f * samLayer + chestFund);
        voicedCore = (1.0f - nasalMix) * voicedCore + nasalMix * nasalWave;

        float voicedGate = 1.0f;
        if (t < plan.onsetGapSec) {
            voicedGate = 0.0f;
            if (plan.hasVoiceBar) {
                voicedCore = 0.25f * lut.sinUnit(phi1);
                voicedGate = 1.0f;
            }
        } else {
            float tVoiced = t - plan.onsetGapSec;
            voicedGate = (1.0f - fastExpNeg(45.0f * tVoiced)) * trillMod * (1.0f - 0.65f * codaBreath);
        }

        float chaosFm = lut.sinUnit(plan.noiseFreq * t + 2.75f * lut.sinUnit(7919.0f * t + 1.85f * lut.sinUnit(13411.0f * t)));
        float burstTime = std::max(0.0f, t - plan.onsetGapSec * 0.75f);
        float onsetNoise = (t >= plan.onsetGapSec * 0.75f)
                               ? (plan.noiseAmp * 0.36f * fastExpNeg(plan.noiseDecay * burstTime) * chaosFm)
                               : 0.0f;

        float breathNoise = (plan.aspiration + codaBreath) * 0.24f * lut.sinUnit(0.5f * phi1)
                            * chaosFm * lut.sinUnit(F2 * t);

        float releaseEnv = fastExpNeg(m_singVoice.trel * 16.0f);
        return releaseEnv * (voicedGate * voicedCore + onsetNoise + breathNoise);
    }

    static const char* midiToSargamSyllable(int relSemitone) {
        static const char* sargam[12] = {
            "sa", "re", "re", "ga", "ga", "ma", "ma", "pa", "dha", "dha", "ni", "ni"
        };
        return sargam[((relSemitone % 12) + 12) % 12];
    }

   
    inline int32_t renderFastHarmoniumSampleQ14(HarmoniumVoice& hv,
                                                uint32_t lfoPhaseInc,
                                                bool posMan,
                                                int32_t manWaveQ14,
                                                int32_t manDepQ14,
                                                int32_t pressureGainQ14) {
        const auto& lut = XpressiveParser::lut();
        const int note = my_clamp(hv.midiNote, 0, 127);
        const uint32_t baseInc = m_midiPhaseIncLut[note];

        
        hv.phaseLfo += lfoPhaseInc;
        const int32_t rawWaveQ14 = posMan ? manWaveQ14 : lut.sinUnitQ14(hv.phaseLfo);
        const int32_t lfoQ14     = (rawWaveQ14 * manDepQ14) >> 14;

      
        const int32_t vibModQ14 = (lfoQ14 * 38) >> 14; // 38/16384 = 0.002319 (-4 cents)
        const int32_t vibDelta  = (static_cast<int32_t>(baseInc >> 10) * vibModQ14) >> 4;
        hv.phaseVib     += static_cast<uint32_t>(static_cast<int32_t>(baseInc) - vibDelta);
        hv.phaseBase    += baseInc;
        hv.phaseCeleste += m_midiCelesteIncLut[note];

    
        const int32_t upperGainQ14 = my_clamp<int32_t>(12288 + (lfoQ14 >> 2), 1638, 20480); // 0.75 + 0.25*lfo
        const int32_t reed1 = lut.sinUnitQ14(hv.phaseVib)
                            - (lut.sinUnitQ14(hv.phaseBase << 1) >> 1)
                            + ((upperGainQ14 * lut.reedUpperUnitQ14(hv.phaseBase)) >> 14);

        const int32_t reed2 = (7373  * lut.reedBaseUnitQ14(hv.phaseCeleste)) >> 14;     // 0.45 * celeste
        const int32_t reed3 = (10650 * lut.reedBaseUnitQ14(hv.phaseBase >> 1)) >> 14;   // 0.65 * sub-octave
        const int32_t reedSumQ14 = reed1 + reed2 + reed3;

        
        if (hv.attackEnvQ14 < 16360) {
            hv.attackEnvQ14 += ((16384 - hv.attackEnvQ14) * m_attackStepQ15) >> 15;
        } else {
            hv.attackEnvQ14 = 16384;
        }

 
        int32_t bellowsEnvQ14 = 13435 + ((2949 * lfoQ14) >> 14);
        bellowsEnvQ14 = (bellowsEnvQ14 * pressureGainQ14) >> 14;

        int32_t envQ14 = (hv.attackEnvQ14 * hv.relEnvQ14) >> 14;
        envQ14 = (envQ14 * bellowsEnvQ14) >> 14;
        envQ14 = (envQ14 * hv.velQ14) >> 14;

        return (reedSumQ14 * envQ14) >> 14;
    }

    inline float midiToFreq(int midiNote) const {
        return m_midiFreqLut[my_clamp(midiNote, 0, 127)];
    }

    void triggerBakedDrum(int trackIdx) {
        if (trackIdx < 0 || trackIdx >= NUM_BAKED_DRUMS) return;
        auto& v = m_drumVoices[m_nextDrumVoice];
        m_nextDrumVoice = (m_nextDrumVoice + 1) % static_cast<int>(m_drumVoices.size());
        v.active = true;
        v.trackIdx = trackIdx;
        v.samplePos = 0;
    }

    void triggerHarmoniumPresetNote(int midiNote, float durSec) {
        for (auto& hv : m_harmVoices) {
            if (hv.active && hv.isAutoPreset) hv.held = false;
        }
        HarmoniumVoice* slot = &m_harmVoices[0];
        for (auto& hv : m_harmVoices) {
            if (!hv.active) { slot = &hv; break; }
        }
        slot->active = true;
        slot->held = true;
        slot->isAutoPreset = true;
        slot->midiNote = midiNote;
        slot->rawMidiNote = midiNote;
        slot->velQ14 = 14746; // 0.90 in Q14
        slot->attackEnvQ14 = 0;
        slot->relEnvQ14 = 16384;
        slot->samplesElapsed = 0;
        slot->autoDurSamples = static_cast<int32_t>(durSec * m_srate);
        slot->phaseLfo = 0;
        slot->phaseVib = 0;
        slot->phaseBase = 0;
        slot->phaseCeleste = 0;
    }

    void triggerSingingVoice(int midiNote, float durSec, bool isPreset, int planIdx) {
        bool wasActive = m_singVoice.active;
        m_singVoice.active = true;
        m_singVoice.held = true;
        m_singVoice.isAutoPreset = isPreset;
        m_singVoice.midiNote = midiNote;
        m_singVoice.targetFreq = midiToFreq(midiNote);
        m_singVoice.t = 0.0f;
        m_singVoice.trel = 0.0f;
        m_singVoice.autoDurSec = durSec;

        if (planIdx >= 0 && planIdx < static_cast<int>(m_precompiledVocalPlans.size())) {
            m_singVoice.plan = m_precompiledVocalPlans[planIdx];
        } else {
            m_singVoice.plan = compileSanskritSyllable(midiToSargamSyllable(midiNote - (60 + m_saSemitone.load())));
        }

        if (!wasActive) {
            m_singVoice.curFreq = m_singVoice.targetFreq;
            m_singVoice.curA1 = m_singVoice.plan.onsetA1;
            m_singVoice.curA3 = m_singVoice.plan.tractScaleA3;
            m_singVoice.phase1 = 0.0f;
            m_singVoice.phase2 = 0.0f;
        }
    }

    std::mutex m_mutex;
    float m_srate = 22050.0f;
    float m_midiFreqLut[128] = {0.0f};
    uint32_t m_midiPhaseIncLut[128] = {0};
    uint32_t m_midiCelesteIncLut[128] = {0};
    uint32_t m_lfoBpmPhaseScale = 0;
    int32_t m_attackStepQ15 = 12;
    int32_t m_relDecayQ15 = 32741;

    PrecompiledFormantSlice m_formantLut[256];
    std::vector<VocalSyllablePlan> m_precompiledVocalPlans;

    float m_vocalChamberBuf[VOCAL_CHAMBER_LEN] = {0};
    int m_vocalChamberIdx = 0;
    int m_tap1 = 0, m_tap2 = 0, m_tap3 = 0;

    int16_t* m_bakedDrums[NUM_BAKED_DRUMS] = {nullptr};
    int m_bakedDrumLen = 0;
    std::array<BakedDrumVoice, 16> m_drumVoices;
    int m_nextDrumVoice = 0;

    int16_t* m_droneLoopBuf = nullptr;
    int m_maxDroneLoopSamples = 0;
    int m_droneLoopLen = 0;
    int m_droneReadIdx = 0;
    std::atomic<bool> m_droneDirty{true};

    std::unique_ptr<XpressiveParser::Node> m_harmoniumAst;
    std::unique_ptr<XpressiveParser::Node> m_singingAst;
    std::unique_ptr<XpressiveParser::Node> m_shrutiO1, m_shrutiO2;
    std::unique_ptr<XpressiveParser::Node> m_tanpuraO1, m_tanpuraO2;

    std::array<HarmoniumVoice, 6> m_harmVoices;
    SingingVoice m_singVoice;

    std::vector<SongNote> m_trackNotes[NUM_TRACKS];
    bool m_trackMuted[NUM_TRACKS] = {false};

    TransferFunctionFX m_liveHarmoniumTf;

    std::atomic<int> m_midiInMode{2};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_harmoniumPresetMode{true};
    std::atomic<bool> m_droneEnabled{false};
    std::atomic<int> m_dronePreset{2};
    std::atomic<int> m_droneSwaraMode{0};

    std::atomic<bool> m_hasSingingData{false};
    std::atomic<bool> m_userSingingEnabled{false};

    std::atomic<bool>  m_bellowsPosManual{false};
    std::atomic<bool>  m_bellowsDepManual{false};
    std::atomic<float> m_manualBellowsPos{0.5f};
    std::atomic<float> m_manualBellowsDep{1.0f};

    std::atomic<int> m_currentTalaIdx{0};
    std::atomic<int> m_currentHarmSongIdx{0};
    std::atomic<int> m_drumLoopTicks{384};
    std::atomic<int> m_currentDrumTick{0};
    int m_totalLoopTicks = 3072;

    std::atomic<int> m_bpm{140};
    std::atomic<int> m_octaveShift{0};
    std::atomic<int> m_saSemitone{0};
    std::atomic<float> m_a4Hz{440.0f};
    std::atomic<int> m_detectedMidiNote{60};

    std::atomic<int> m_countdownBeats{0};
    float m_countdownTimerSec = 0.0f;
    float m_clickEnv = 0.0f;
    float m_songTickPos = 0.0f;
    int m_lastTriggeredTick = -1;
};

#endif // SYNTHENGINE_H
