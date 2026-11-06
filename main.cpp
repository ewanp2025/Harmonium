#include "bt_audio.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "i18n.h"
#include "synthengine.h"


#ifdef ESP32
#include <Arduino.h>
#include <driver/i2s.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "display_driver.h"
#include "usb_midi_host.h"
#include "storage.h"
#include "usb_audio.h"
#include "wifi_stream.h"

#define I2S_BCLK_PIN  6
#define I2S_DOUT_PIN  15
#define I2S_LRCK_PIN  16
#define AUDIO_SRATE   22050

static Waveshare7Display g_display;
static Waveshare7Display* g_tft = &g_display;
static UsbMidiHost usbMidi;
static UsbAudioOut g_usbAudio;

#else
#include <QApplication>
#include <QIODevice>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QTimer>
#include "display_driver_qt.h"
#include "midireader.h"

static Waveshare7QtDisplay* g_tft = nullptr;

inline void initFileSystem() {}
inline std::vector<std::string> listBankFiles() { return {"/Default_Bank.mhb"}; }
inline bool saveBankToFile(const char*, int, int) { return true; }
inline bool loadBankFromFile(const char*, int&, int&) { return true; }
#endif

static SynthEngine engine;
static bool g_midiConnected = false;

static const char* WESTERN_NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// EDITOR STATE & CONSTANTS

enum AppView { VIEW_LIVE, VIEW_TALA_ED, VIEW_HARM_ED };
static AppView g_currentView = VIEW_LIVE;

enum ModalState { MODAL_NONE, MODAL_BOL, MODAL_KARTAL, MODAL_SWARA, MODAL_FILE_LOAD, MODAL_SAVE_KBD, MODAL_BT_PAIR };
static ModalState g_modalState = MODAL_NONE;
static int g_editingCellIdx = -1;
static std::vector<std::string> g_fileList;
static std::string g_kbdInput;
static int g_audioOutMode = 1; // 0 = USB, 1 = I2S, 2 = WIFI

static const char* VALID_DIVISIONS[] = {"4+4+4+4", "4+4", "3+3", "4+4+4", "3+2+2", "2+3+2+3"};

static const char* VALID_BOLS[] = {
    "-", "Dha", "Dhin", "Ge", "Ka", "Na", "Ta", "Ti", "Re",
    "Ti Re", "Re Ti", "Ta Dhin", "Ti Ka", "Ta Ge", "Dha Ge", "Dhin Na", "Ka Ta", "Ti Re Ka Ta", "Ka Dha",
    "Ra", "Ki"
};
static const int NUM_VALID_BOLS = 21;

static const char* VALID_KARTALS[] = {
    "-", "Kartal Open", "Kartal Closed", "Closed+Closed", "Manjira", "Ghanta", "Kansa", "Damaru", "Shankha"
};
static const int NUM_VALID_KARTALS = 9;

static const char* VALID_SWARAS[] = {
    "-", ".", "'P", "'d", "'D", "'n", "'N",
    "S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N",
    "S'", "r'", "R'", "g'", "S R", "R g", "m P", "d n"
};
static const int NUM_VALID_SWARAS = 27;

static int  g_langIdx = 0;
static bool g_useCountdown = true;
static int  g_touchHeldMidi = -1;
static int  g_lastDisplayedMidi = -999;
static int  g_lastDisplayedMatra = -1;
static bool g_lastMidiConnected = false;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const I18n::LangPack& L() {
    const auto& langs = I18n::getLanguages();
    int idx = std::clamp(g_langIdx, 0, static_cast<int>(langs.size()) - 1);
    return langs[idx];
}

struct TouchButton {
    int x, y, w, h;
};

static inline bool hitBox(const TouchButton& b, int tx, int ty) {
    return (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h);
}


// HELPER FUNCTIONS

static std::vector<int> parseVibhags(const char* divStr) {
    std::vector<int> res;
    if (!divStr) return {4, 4};
    const char* p = divStr;
    while (*p) {
        while (*p && !std::isdigit(static_cast<unsigned char>(*p))) p++;
        if (!*p) break;
        int v = std::strtol(p, const_cast<char**>(&p), 10);
        if (v > 0) res.push_back(v);
    }
    if (res.empty()) res = {4, 4};
    return res;
}

static std::vector<std::string> getTaliKhaliLabels(const std::vector<int>& divs) {
    std::vector<std::string> res;
    int n = static_cast<int>(divs.size());
    int taliNum = 2;
    for (int i = 0; i < n; ++i) {
        if (i == 0) res.push_back("X");
        else if (n == 4 && i == 2) res.push_back("0");
        else if (n == 2 && i == 1) res.push_back("0");
        else res.push_back(std::to_string(taliNum++));
    }
    return res;
}


// UI LAYOUT CONSTANTS

static const TouchButton TAB_LIVE  = { 16,  6, 110, 34};
static const TouchButton TAB_TALA  = {130,  6, 130, 34};
static const TouchButton TAB_HARM  = {264,  6, 130, 34};
static const TouchButton BTN_AUDIO = {398,  6, 130, 34};
static const TouchButton BTN_BT    = {532,  6, 150, 34};
static const TouchButton BTN_LANG  = {686,  6,  98, 34};

// Live View Controls
static const TouchButton BTN_START     = {  6,  46,  86, 34};
static const TouchButton BTN_PAUSE     = { 96,  46,  72, 34};
static const TouchButton BTN_COUNT     = {172,  46,  58, 34};
static const TouchButton BTN_TALA_PREV = {236,  46,  28, 34};
static const TouchButton BTN_TALA_NEXT = {424,  46,  28, 34};
static const TouchButton BTN_SONG_PREV = {458,  46,  28, 34};
static const TouchButton BTN_SONG_NEXT = {664,  46,  28, 34};
static const TouchButton BTN_BPM_DOWN  = {698,  46,  32, 34};
static const TouchButton BTN_BPM_UP    = {762,  46,  32, 34};

// Editor Layout Shared Controls
static const TouchButton BTN_ED_PREV  = {16,  46,  40, 34};
static const TouchButton BTN_ED_NAME  = {60,  46, 200, 34};
static const TouchButton BTN_ED_NEXT  = {264, 46,  40, 34};
static const TouchButton BTN_ED_NEW   = {312, 46,  80, 34};
static const TouchButton BTN_ED_RAND  = {400, 46, 110, 34};
static const TouchButton BTN_ED_PLAY  = {518, 46, 110, 34};
static const TouchButton BTN_ED_SAVE  = {636, 46, 100, 34};

static const TouchButton BTN_ED_DIVS  = {16,  86, 130, 34};
static const TouchButton BTN_ED_LOAD  = {636, 86, 100, 34};

static const TouchButton BTN_SA_CYCLE  = {190,  86, 155, 34};
static const TouchButton BTN_DRONE_EN  = {350,  86, 115, 34};
static const TouchButton BTN_DRONE_PRE = {470,  86, 155, 34};
static const TouchButton BTN_DRONE_SUR = {630,  86, 164, 34};

// Utility Row Controls
static const TouchButton BTN_RND_TALA  = {  6, 252, 150, 28};
static const TouchButton BTN_RND_SONG  = {162, 252, 150, 28};
static const TouchButton BTN_MIDI_MODE = {318, 252, 200, 28};
static const TouchButton BTN_SING_MODE = {524, 252, 270, 28};

static const TouchButton BTN_HARM_MODE = {  6, 286, 330, 28};
static const TouchButton BTN_OCT_DOWN  = {342, 286,  56, 28};
static const TouchButton BTN_OCT_UP    = {462, 286,  56, 28};
static const TouchButton BTN_TF_EN     = {524, 286, 130, 28};
static const TouchButton BTN_EDIT_MODE = {660, 286, 134, 28};

static const int PIANO_Y = 320;
static const int PIANO_H = 134;
static const int PIANO_START_MIDI = 48;
static const int PIANO_NUM_KEYS   = 37;


// TOUCH PIANO LOGIC

static inline bool isBlackKey(int midi) {
    int n = midi % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

static int hitTestPiano(int tx, int ty) {
    if (ty < PIANO_Y || ty >= PIANO_Y + PIANO_H) return -1;
    int whiteCount = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        if (!isBlackKey(PIANO_START_MIDI + i)) whiteCount++;
    }
    float whiteW = 800.0f / whiteCount;
    int blackH = static_cast<int>(PIANO_H * 0.60f);

    if (ty - PIANO_Y <= blackH) {
        int wIdx = 0;
        for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
            int midi = PIANO_START_MIDI + i;
            if (!isBlackKey(midi)) {
                wIdx++;
            } else {
                int x = static_cast<int>((wIdx - 0.33f) * whiteW);
                int w = static_cast<int>(whiteW * 0.66f);
                if (tx >= x && tx <= x + w) return midi;
            }
        }
    }

    int targetWhite = static_cast<int>(tx / whiteW);
    int wIdx = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        int midi = PIANO_START_MIDI + i;
        if (!isBlackKey(midi)) {
            if (wIdx == targetWhite) return midi;
            wIdx++;
        }
    }
    return -1;
}


// DRAWING ROUTINES


static void drawButton(const TouchButton& b, const char* label, uint16_t bgCol, uint16_t borderCol = 0x5AEB, uint16_t textCol = 0xFFFF) {
    g_tft->fillRoundRect(b.x, b.y, b.w, b.h, 4, bgCol);
    g_tft->drawRoundRect(b.x, b.y, b.w, b.h, 4, borderCol);
    g_tft->setTextColor(textCol, bgCol);
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

static void drawTabs() {
    drawButton(TAB_LIVE, "<- BACK", g_currentView == VIEW_LIVE ? rgb565(0, 105, 92) : rgb565(30, 40, 50));
    drawButton(TAB_TALA, L().tabTalaEditor, g_currentView == VIEW_TALA_ED ? rgb565(0, 105, 92) : rgb565(30, 40, 50));
    drawButton(TAB_HARM, L().tabHarmEditor, g_currentView == VIEW_HARM_ED ? rgb565(0, 105, 92) : rgb565(30, 40, 50));

#ifdef ESP32
    const char* audLabels[] = {g_usbAudio.getStatusLabel(), "OUT: I2S", "OUT: WIFI"};
    uint16_t audBg = (g_audioOutMode == 0 && g_usbAudio.isConnected())
                         ? rgb565(27, 94, 32)
                         : rgb565(37, 40, 48);
    drawButton(BTN_AUDIO, audLabels[g_audioOutMode], audBg, rgb565(0, 229, 255), 0xFFFF);
#else
    drawButton(BTN_AUDIO, "OUT: QT PC", rgb565(37, 40, 48), rgb565(0, 229, 255), 0xFFFF);
#endif

    drawButton(BTN_LANG, L().menuLabel, rgb565(30, 41, 59), rgb565(0, 229, 255), rgb565(0, 229, 255));
    
    auto& bt = BtAudioManager::instance();
    std::string btLbl = bt.isConnected() ? ("BT: " + bt.getSavedName().substr(0, 10)) : "BT PAIR/AUDIO";
    drawButton(BTN_BT, btLbl.c_str(), bt.isConnected() ? rgb565(2, 119, 189) : rgb565(37, 40, 48), rgb565(0, 229, 255), 0xFFFF);
}

static void drawEditorNavRow(const char* name, const char* divisions) {
    const auto& lang = L();
    drawButton(BTN_ED_PREV, "<", rgb565(74, 20, 140));

    std::string shortName = name;
    if (shortName.length() > 18) shortName = shortName.substr(0, 18) + "..";
    drawButton(BTN_ED_NAME, shortName.c_str(), rgb565(37, 40, 48), rgb565(255, 202, 40));
    drawButton(BTN_ED_NEXT, ">", rgb565(74, 20, 140));

    drawButton(BTN_ED_NEW, lang.btnEdNew, rgb565(0, 105, 92));
    drawButton(BTN_ED_RAND, lang.btnEdRand, rgb565(106, 27, 154), rgb565(255, 235, 59));

    bool playing = engine.isPlaying();
    drawButton(BTN_ED_PLAY, lang.btnEdPlay, playing ? rgb565(183, 28, 28) : rgb565(27, 94, 32));
    drawButton(BTN_ED_SAVE, lang.btnEdSave, rgb565(46, 125, 50));

    std::string divStr = std::string(lang.lblEdDivisions) + divisions;
    drawButton(BTN_ED_DIVS, divStr.c_str(), rgb565(38, 50, 56), rgb565(0, 172, 193));
    drawButton(BTN_ED_LOAD, lang.btnEdLoad, rgb565(2, 119, 189));
}

static void drawFileSelector() {
    g_tft->fillRect(40, 30, 720, 420, rgb565(15, 20, 25));
    g_tft->drawRect(40, 30, 720, 420, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString("SELECT A .MHB BANK FILE TO LOAD (Tap outside to cancel)", 400, 50);

    for (size_t i = 0; i < g_fileList.size() && i < 12; ++i) {
        TouchButton btn = {80 + static_cast<int>(i % 3) * 220, 100 + static_cast<int>(i / 3) * 60, 200, 45};
        drawButton(btn, g_fileList[i].c_str(), rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);
    }
    if (g_fileList.empty()) {
        g_tft->drawString("No .mhb files found in LittleFS storage.", 400, 200);
    }
}

static void drawKeyboard() {
    g_tft->fillRect(40, 30, 720, 420, rgb565(15, 20, 25));
    g_tft->drawRect(40, 30, 720, 420, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString("ENTER FILE NAME TO SAVE (without .mhb):", 400, 60);

    // Input Box
    g_tft->fillRect(100, 100, 600, 50, rgb565(37, 40, 48));
    g_tft->drawRect(100, 100, 600, 50, rgb565(255, 202, 40));
    g_tft->setTextColor(0xFFFF, rgb565(37, 40, 48));
    std::string disp = g_kbdInput + "_";
    g_tft->drawString(disp.c_str(), 400, 125);

    const char* rows[] = {"1234567890-", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_"};
    int yStart = 170;
    for (int r = 0; r < 4; ++r) {
        int len = strlen(rows[r]);
        int xStart = 400 - (len * 45) / 2;
        for (int c = 0; c < len; ++c) {
            TouchButton btn = {xStart + c * 45, yStart + r * 50, 40, 40};
            char k[2] = {rows[r][c], '\0'};
            drawButton(btn, k, rgb565(55, 71, 79), rgb565(0, 172, 193), 0xFFFF);
        }
    }
    drawButton({200, 380, 120, 45}, "CANCEL", rgb565(183, 28, 28));
    drawButton({340, 380, 120, 45}, "SPACE", rgb565(55, 71, 79));
    drawButton({480, 380, 120, 45}, "SAVE", rgb565(46, 125, 50));
    drawButton({630, 170, 80, 40}, "DEL", rgb565(230, 81, 0));
}

static void drawTopTransportRow() {
    const auto& lang = L();
    drawButton(BTN_START, engine.isPlaying() ? lang.btnStop : lang.btnStart,
               engine.isPlaying() ? rgb565(183, 28, 28) : rgb565(27, 94, 32));
    drawButton(BTN_PAUSE, engine.isPaused() ? lang.btnResume : lang.btnPause, rgb565(230, 81, 0));

    std::string cntLbl = std::string(lang.chkCountdown) + ":" + (g_useCountdown ? lang.stateOn : lang.stateOff);
    drawButton(BTN_COUNT, cntLbl.c_str(),
               g_useCountdown ? rgb565(0, 105, 92) : rgb565(55, 65, 81));

    drawButton(BTN_TALA_PREV, "<", rgb565(0, 77, 64));
    TouchButton talaBox = {266, 46, 156, 34};
    std::string talaName = BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name;
    if (talaName.size() > 18) talaName = talaName.substr(0, 18);
    drawButton(talaBox, talaName.c_str(), rgb565(0, 77, 64), rgb565(0, 229, 255));
    drawButton(BTN_TALA_NEXT, ">", rgb565(0, 77, 64));

    drawButton(BTN_SONG_PREV, "<", rgb565(74, 20, 140));
    TouchButton songBox = {488, 46, 174, 34};
    std::string songName = BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title;
    if (songName.size() > 20) songName = songName.substr(0, 20);
    drawButton(songBox, songName.c_str(), rgb565(74, 20, 140), rgb565(255, 202, 40));
    drawButton(BTN_SONG_NEXT, ">", rgb565(74, 20, 140));

    drawButton(BTN_BPM_DOWN, "-5", rgb565(43, 48, 60));
    TouchButton bpmBox = {731, 46, 30, 34};
    char bpmStr[12];
    std::snprintf(bpmStr, sizeof(bpmStr), "%d", engine.getBpm());
    drawButton(bpmBox, bpmStr, rgb565(30, 34, 42));
    drawButton(BTN_BPM_UP, "+5", rgb565(43, 48, 60));
}

static void drawSwaraPopup() {
    const auto& lang = L();
    TouchButton swaraBox = {6, 86, 178, 34};
    char buf[64];
    if (engine.isPlaying() && engine.getCountdownBeats() > 0) {
        std::snprintf(buf, sizeof(buf), "%s %d", lang.countdownPrefix, engine.getCountdownBeats());
    } else {
        int midi = engine.getDetectedMidiNote();
        int sa = engine.getSaSemitone();
        int rel = midi - (60 + sa);
        int semi = ((rel % 12) + 12) % 12;
        int octOff = static_cast<int>(std::floor(rel / 12.0f));
        std::string sw = lang.swaraNames[semi];
        if (octOff < 0) sw = "'" + sw;
        else if (octOff > 0) sw += "'";
        int westOct = (midi / 12) - 1;
        std::snprintf(buf, sizeof(buf), "%s %s (%s%d)", lang.swaraPopupPrefix, sw.c_str(), WESTERN_NOTES[midi % 12], westOct);
    }
    drawButton(swaraBox, buf, rgb565(13, 37, 56), rgb565(0, 229, 255), rgb565(0, 229, 255));
}

static void drawDroneAndTunerRow() {
    const auto& lang = L();
    drawSwaraPopup();

    std::string saStr = std::string(lang.saKeyPrefix) + lang.indianKeyLabels[engine.getSaSemitone()];
    drawButton(BTN_SA_CYCLE, saStr.c_str(), rgb565(37, 40, 48));

    bool dEn = engine.isDroneEnabled();
    drawButton(BTN_DRONE_EN, dEn ? lang.btnDroneOn : lang.btnDroneOff,
               dEn ? rgb565(245, 127, 23) : rgb565(66, 66, 66),
               0xFFFF, dEn ? 0x0000 : 0xFFFF);

    const char* presetLbl = lang.dronePresetBoth;
    if (engine.getDronePreset() == 0) presetLbl = lang.dronePresetShruti;
    else if (engine.getDronePreset() == 1) presetLbl = lang.dronePresetTanpura;
    drawButton(BTN_DRONE_PRE, presetLbl, rgb565(37, 40, 48));

    const char* surLbls[4] = {lang.droneSurSaPa, lang.droneSurSaMa, lang.droneSurSaNi, lang.droneSurSaOnly};
    drawButton(BTN_DRONE_SUR, surLbls[engine.getDroneSwaraMode() % 4], rgb565(37, 40, 48));
}

static TouchButton getTrackButtonRect(int idx) {
    int col = idx % 6;
    int row = idx / 6;
    return {6 + col * 132, 142 + row * 27, 130, 23};
}

static void drawTrackButtons() {
    const auto& lang = L();
    const auto& trks = BuiltInPresets::getTracks();
    int numTracks = std::min(static_cast<int>(trks.size()), SynthEngine::NUM_TRACKS);
    for (int i = 0; i < numTracks; ++i) {
        TouchButton b = getTrackButtonRect(i);
        if (i == SynthEngine::TRACK_SINGING && !engine.hasSingingData()) {
            g_tft->fillRect(b.x, b.y, b.w, b.h, rgb565(22, 25, 30));
            continue;
        }

        bool muted = engine.isTrackMuted(i);
        std::string dispName = lang.translateTrackName(trks[i].name);
        if (dispName == "Singing_Voice") dispName = (g_langIdx == 1) ? "गायन (Voice)" : "Voice";
        std::string lbl = dispName + " [" + (muted ? lang.stateOff : lang.stateOn) + "]";
        drawButton(b, lbl.c_str(),
                   muted ? rgb565(55, 71, 79) : rgb565(46, 125, 50),
                   muted ? rgb565(85, 85, 85) : rgb565(129, 199, 132));
    }
}

static void drawUtilityRow() {
    const auto& lang = L();
    drawButton(BTN_RND_TALA, "RND TALA", rgb565(0, 121, 107), rgb565(255, 235, 59));
    drawButton(BTN_RND_SONG, "RND RAGA", rgb565(106, 27, 154), rgb565(255, 235, 59));

    int m = engine.getMidiInMode();
    const char* mStr = (m == 0) ? "IN: DUAL (H+D)" : (m == 1) ? "IN: SPLIT (<52)" : (m == 2) ? "IN: HARM ONLY" : "IN: DRUM ONLY";
    drawButton(BTN_MIDI_MODE, mStr, rgb565(55, 71, 79));

    if (engine.hasSingingData()) {
        bool singOn = engine.isSingingEnabled();
        std::string singLbl = (g_langIdx == 1)
                                  ? (std::string("गायन (VOICE): ") + (singOn ? lang.stateOn : lang.stateOff))
                                  : (std::string("SINGING VOICE: ") + (singOn ? lang.stateOn : lang.stateOff));
        drawButton(BTN_SING_MODE, singLbl.c_str(),
                   singOn ? rgb565(194, 24, 91) : rgb565(55, 71, 79),
                   singOn ? rgb565(255, 128, 171) : rgb565(100, 116, 139));
    } else {
        g_tft->fillRect(BTN_SING_MODE.x, BTN_SING_MODE.y, BTN_SING_MODE.w, BTN_SING_MODE.h, rgb565(22, 25, 30));
    }
}

static void drawHarmoniumDspRow() {
    const auto& lang = L();
    bool presetOn = engine.isHarmoniumPresetMode();
    drawButton(BTN_HARM_MODE,
               presetOn ? lang.btnHarmPresetOn : lang.btnHarmPresetOff,
               presetOn ? rgb565(239, 108, 0) : rgb565(0, 105, 92));

    drawButton(BTN_OCT_DOWN, lang.btnOctDown, rgb565(43, 48, 60));
    TouchButton octLbl = {400, 286, 60, 28};
    char octBuf[32];
    std::snprintf(octBuf, sizeof(octBuf), "%s %d", lang.lblOctavePrefix, engine.getOctaveShift());
    drawButton(octLbl, octBuf, rgb565(22, 25, 30));
    drawButton(BTN_OCT_UP, lang.btnOctUp, rgb565(43, 48, 60));

    bool tfOn = engine.isTfEnabled();
    drawButton(BTN_TF_EN, tfOn ? "FX: ON" : "FX: OFF",
               tfOn ? rgb565(40, 53, 147) : rgb565(51, 51, 51));

    drawButton(BTN_EDIT_MODE, "EDITORS", rgb565(60, 60, 70));
}

static void drawTalometerLEDs() {
    g_tft->fillRect(0, 120, 800, 20, rgb565(22, 25, 30));

    int curMatra = engine.getCurrentMatra() - 1;
    int talaIdx = engine.getSelectedTala();
    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[talaIdx];

    auto vibhags = parseVibhags(kt.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);

    int totalMatras = 0;
    for (int v : vibhags) totalMatras += v;
    if (totalMatras <= 0) return;

    int startX = 400 - (totalMatras * 28) / 2;
    int yCenter = 130;

    int gMatra = 0;
    for (size_t v = 0; v < vibhags.size(); ++v) {
        bool isKhali = (tkLabels[v] == "0");

        for (int m = 0; m < vibhags[v]; ++m) {
            int cx = startX + gMatra * 28 + 14;
            bool active = (gMatra == curMatra && engine.isPlaying() && engine.getCountdownBeats() == 0);
            uint16_t color;

            if (m == 0) {
                if (isKhali) {
                    color = active ? rgb565(0, 255, 0) : rgb565(0, 70, 0);
                } else {
                    color = active ? rgb565(255, 50, 50) : rgb565(100, 0, 0);
                }
            } else {
                color = active ? rgb565(255, 202, 40) : rgb565(80, 60, 0);
            }
            (void)cx;
            (void)yCenter;
            (void)color;
            gMatra++;
        }
    }
}

static void drawPianoKeyboard(int activeMidi = -1) {
    const auto& lang = L();
    int whiteCount = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        if (!isBlackKey(PIANO_START_MIDI + i)) whiteCount++;
    }
    float whiteW = 800.0f / whiteCount;
    int blackH = static_cast<int>(PIANO_H * 0.60f);
    int sa = engine.getSaSemitone();

    int wIdx = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        int midi = PIANO_START_MIDI + i;
        if (isBlackKey(midi)) continue;

        int x = static_cast<int>(wIdx * whiteW);
        int w = static_cast<int>((wIdx + 1) * whiteW) - x - 1;
        bool active = (midi == activeMidi);
        bool isSa = ((midi % 12) == sa);

        uint16_t fillCol = active ? rgb565(0, 229, 255)
                                  : (isSa ? rgb565(255, 243, 205) : rgb565(245, 245, 245));
        g_tft->fillRect(x, PIANO_Y, w, PIANO_H, fillCol);
        g_tft->drawRect(x, PIANO_Y, w, PIANO_H, rgb565(51, 51, 51));

        int rel = (midi - sa + 120) % 12;
        g_tft->setTextColor(0x0000, fillCol);
        g_tft->setTextDatum(BC_DATUM);
        g_tft->drawString(lang.swaraNames[rel], x + w / 2, PIANO_Y + PIANO_H - 18);
        g_tft->drawString(WESTERN_NOTES[midi % 12], x + w / 2, PIANO_Y + PIANO_H - 3);
        wIdx++;
    }

    wIdx = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        int midi = PIANO_START_MIDI + i;
        if (!isBlackKey(midi)) {
            wIdx++;
            continue;
        }
        int x = static_cast<int>((wIdx - 0.33f) * whiteW);
        int w = static_cast<int>(whiteW * 0.66f);
        bool active = (midi == activeMidi);
        uint16_t fillCol = active ? rgb565(255, 152, 0) : rgb565(27, 27, 27);

        g_tft->fillRect(x, PIANO_Y, w, blackH, fillCol);
        g_tft->drawRect(x, PIANO_Y, w, blackH, 0x0000);

        int rel = (midi - sa + 120) % 12;
        g_tft->setTextColor(0xFFFF, fillCol);
        g_tft->setTextDatum(BC_DATUM);
        g_tft->drawString(lang.swaraNames[rel], x + w / 2, PIANO_Y + blackH - 4);
    }
}

static void drawFooter() {
    const auto& lang = L();
    g_tft->fillRect(0, 456, 800, 24, rgb565(22, 25, 30));
    g_tft->setTextColor(rgb565(200, 200, 200), rgb565(22, 25, 30));
    g_tft->setTextDatum(ML_DATUM);

    const char* sTitle = BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title;
    const char* tName  = BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name;
    char leftBuf[160];
    std::snprintf(leftBuf, sizeof(leftBuf), "%s: %s | %s: %s (%s %d/%d)",
                  lang.footerSongLbl, sTitle, lang.footerTalaLbl, tName,
                  lang.footerMatraLbl, engine.getCurrentMatra(), engine.getDrumLoopMatras());
    g_tft->drawString(leftBuf, 8, 468);

    g_tft->setTextDatum(MR_DATUM);
    g_tft->setTextColor(g_midiConnected ? rgb565(0, 229, 255) : rgb565(160, 160, 160),
                        rgb565(22, 25, 30));
    g_tft->drawString(g_midiConnected ? lang.midiConnected : lang.midiPcMode, 792, 468);
}


// WINDOWS EDITOR LAYOUTS


static void drawTalaEditor() {
    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[engine.getSelectedTala()];
    drawEditorNavRow(kt.name, kt.divisions);

    auto vibhags = parseVibhags(kt.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);
    int globalMatra = 0;

    for (size_t v = 0; v < vibhags.size(); ++v) {
        for (int m = 0; m < vibhags[v]; ++m) {
            int screenRow = globalMatra / 8;
            int screenCol = globalMatra % 8;
            int bx = 16 + screenCol * 96;
            int by = 130 + screenRow * 120;

            char mBuf[32];
            if (m == 0) std::snprintf(mBuf, sizeof(mBuf), "[%s] M%d", tkLabels[v].c_str(), globalMatra + 1);
            else        std::snprintf(mBuf, sizeof(mBuf), "M%d", globalMatra + 1);

            TouchButton headerBtn = {bx, by, 90, 24};
            drawButton(headerBtn, mBuf, rgb565(38, 50, 56), rgb565(0, 172, 193), rgb565(0, 229, 255));

            TouchButton bolBtn = {bx, by + 28, 90, 36};
            const char* curBol = (globalMatra < static_cast<int>(kt.bolCells.size())) ? kt.bolCells[globalMatra] : "-";
            drawButton(bolBtn, curBol, rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);

            TouchButton kartalBtn = {bx, by + 68, 90, 36};
            const char* curKar = (globalMatra < static_cast<int>(kt.kartalCells.size())) ? kt.kartalCells[globalMatra] : "-";
            drawButton(kartalBtn, curKar, rgb565(26, 35, 46), rgb565(60, 60, 60), rgb565(200, 200, 200));

            globalMatra++;
        }
    }
}

static void drawHarmEditor() {
    auto& hs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs())[engine.getSelectedSong()];
    drawEditorNavRow(hs.title, hs.divisions);

    if (hs.forms.empty()) return;
    auto& form = hs.forms[0];
    auto vibhags = parseVibhags(hs.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);

    int totalMatras = 0;
    for (int v : vibhags) totalMatras += v;
    int cellW = (totalMatras > 0) ? (768 / totalMatras) : 48;
    if (cellW > 64) cellW = 64;

    int globalMatra = 0;
    for (size_t v = 0; v < vibhags.size(); ++v) {
        for (int m = 0; m < vibhags[v]; ++m) {
            char mBuf[32];
            if (m == 0) std::snprintf(mBuf, sizeof(mBuf), "[%s] M%d", tkLabels[v].c_str(), globalMatra + 1);
            else        std::snprintf(mBuf, sizeof(mBuf), "M%d", globalMatra + 1);

            TouchButton hBtn = {16 + globalMatra * cellW, 130, cellW - 2, 24};
            drawButton(hBtn, mBuf, (m == 0) ? rgb565(38, 50, 56) : rgb565(30, 34, 42),
                       (m == 0) ? rgb565(0, 172, 193) : rgb565(85, 85, 85),
                       (m == 0) ? rgb565(0, 229, 255) : rgb565(170, 170, 170));
            globalMatra++;
        }
    }

    for (int r = 0; r < form.numRows; ++r) {
        for (int c = 0; c < totalMatras; ++c) {
            int cellIdx = r * totalMatras + c;
            TouchButton sBtn = {16 + c * cellW, 160 + r * 42, cellW - 2, 38};
            const char* curSwara = (cellIdx < static_cast<int>(form.swaraCells.size())) ? form.swaraCells[cellIdx] : "-";
            drawButton(sBtn, curSwara, rgb565(26, 36, 46), rgb565(0, 172, 193), 0xFFFF);
        }
    }
}

static void drawModalSelector() {
    g_tft->fillRect(40, 30, 720, 420, rgb565(15, 20, 25));
    g_tft->drawRect(40, 30, 720, 420, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString("SELECT VALUE (Tap to apply, tap outside to cancel)", 400, 50);

    const char** list = nullptr;
    int count = 0;

    if (g_modalState == MODAL_BOL) { list = VALID_BOLS; count = NUM_VALID_BOLS; }
    else if (g_modalState == MODAL_KARTAL) { list = VALID_KARTALS; count = NUM_VALID_KARTALS; }
    else if (g_modalState == MODAL_SWARA) { list = VALID_SWARAS; count = NUM_VALID_SWARAS; }

    int cols = 6;
    int cellW = 100, cellH = 45;
    int startX = 400 - (cols * (cellW + 10)) / 2;
    int startY = 80;

    for (int i = 0; i < count; ++i) {
        int c = i % cols;
        int r = i / cols;
        TouchButton btn = {startX + c * (cellW + 10), startY + r * (cellH + 10), cellW, cellH};
        drawButton(btn, list[i], rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);
    }
}

static void drawBtModal() {
    auto& bt = BtAudioManager::instance();
    g_tft->fillRect(40, 30, 720, 420, rgb565(15, 20, 25));
    g_tft->drawRect(40, 30, 720, 420, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString("BLUETOOTH PAIRING & DEVICE MANAGER", 400, 50);

    g_tft->setTextColor(rgb565(0, 229, 255), rgb565(15, 20, 25));
    g_tft->drawString(bt.getStatusText().c_str(), 400, 74);

    drawButton({60,  94, 190, 36}, bt.isScanning() ? "SCANNING..." : "SCAN PAIR MODE", rgb565(0, 105, 92));
    drawButton({265, 94, 190, 36}, "FORGET SAVED", rgb565(183, 28, 28));
    drawButton({620, 94, 120, 36}, "CLOSE", rgb565(55, 71, 79));

    const auto& devs = bt.getDevices();
    for (size_t i = 0; i < devs.size() && i < 8; ++i) {
        TouchButton b = {60 + static_cast<int>(i % 2) * 345, 145 + static_cast<int>(i / 2) * 68, 335, 56};
        bool isSaved = (!bt.getSavedMac().empty() && devs[i].mac == bt.getSavedMac());
        char lbl[80];
        std::snprintf(lbl, sizeof(lbl), "%s (%ddBm)%s",
                      devs[i].name.substr(0, 18).c_str(), devs[i].rssi, isSaved ? " [PAIRED]" : "");
        drawButton(b, lbl, isSaved ? rgb565(27, 94, 32) : rgb565(37, 40, 48),
                   isSaved ? rgb565(129, 199, 132) : rgb565(0, 172, 193), 0xFFFF);
    }
    if (devs.empty()) {
        g_tft->setTextColor(rgb565(180, 180, 180), rgb565(15, 20, 25));
        g_tft->drawString("Put speaker/device in Pairing Mode and tap SCAN PAIR MODE", 400, 260);
    }
}


static void redrawEntireScreen() {
#ifndef ESP32
    g_tft->setWindowTitle(QString::fromUtf8(L().windowTitle));
#endif
    g_tft->fillScreen(rgb565(22, 25, 30));
    drawTabs();

    if (g_modalState == MODAL_FILE_LOAD) {
        drawFileSelector();
        return;
    }
    if (g_modalState == MODAL_SAVE_KBD) {
        drawKeyboard();
        return;
    }
    if (g_modalState == MODAL_BT_PAIR) {
        drawBtModal();
        return;
    }
    
    if (g_modalState != MODAL_NONE) {
        if (g_currentView == VIEW_TALA_ED) drawTalaEditor();
        else if (g_currentView == VIEW_HARM_ED) drawHarmEditor();
        drawModalSelector();
        return;
    }

    if (g_currentView == VIEW_LIVE) {
        drawTopTransportRow();
        drawDroneAndTunerRow();
        drawTalometerLEDs();
        drawTrackButtons();
        drawUtilityRow();
        drawHarmoniumDspRow();
        drawPianoKeyboard(g_touchHeldMidi);
        drawFooter();
    } else if (g_currentView == VIEW_TALA_ED) {
        drawTalaEditor();
    } else if (g_currentView == VIEW_HARM_ED) {
        drawHarmEditor();
    }
}

static bool wasTouching = false;

static void esp32LoopTick() {
    uint16_t tx = 0, ty = 0;
    bool touching = g_tft->getTouch(&tx, &ty);

    if (touching && !wasTouching) {
        if (g_modalState == MODAL_BT_PAIR) {
            auto& bt = BtAudioManager::instance();
            if (hitBox({60, 94, 190, 36}, tx, ty)) {
                bt.startScan();
            } else if (hitBox({265, 94, 190, 36}, tx, ty)) {
                bt.forgetPairedDevice();
            } else if (hitBox({620, 94, 120, 36}, tx, ty)) {
                g_modalState = MODAL_NONE;
            } else {
                const auto& devs = bt.getDevices();
                for (size_t i = 0; i < devs.size() && i < 8; ++i) {
                    TouchButton b = {60 + static_cast<int>(i % 2) * 345, 145 + static_cast<int>(i / 2) * 68, 335, 56};
                    if (hitBox(b, tx, ty)) {
                        bt.selectAndPairDevice(static_cast<int>(i));
                        break;
                    }
                }
            }
            redrawEntireScreen();
        } else if (hitBox(BTN_BT, tx, ty)) {
            g_modalState = MODAL_BT_PAIR;
            redrawEntireScreen();
} else if (hitBox(BTN_AUDIO, tx, ty)) {
            // Audio Routing Toggle: 0 = USB Audio Out, 1 = I2S (USB MIDI In), 2 = WIFI (USB MIDI In)
            g_audioOutMode = (g_audioOutMode + 1) % 3;
#ifdef ESP32
            UsbAudioPartner::s_enableUsbAudio = (g_audioOutMode == 0);
#endif
            redrawEntireScreen();
        } else if (g_modalState == MODAL_FILE_LOAD) {
            for (size_t i = 0; i < g_fileList.size() && i < 12; ++i) {
                TouchButton btn = {80 + static_cast<int>(i % 3) * 220, 100 + static_cast<int>(i / 3) * 60, 200, 45};
                if (hitBox(btn, tx, ty)) {
                    int nTala = 0, nSong = 0;
                    std::string fullPath = "/" + g_fileList[i];
                    if (loadBankFromFile(fullPath.c_str(), nTala, nSong)) {
                        engine.selectTala(nTala);
                        engine.selectHarmoniumSong(nSong);
                    }
                    break;
                }
            }
            g_modalState = MODAL_NONE;
            redrawEntireScreen();
        } else if (g_modalState == MODAL_SAVE_KBD) {
            bool handled = false;
            const char* rows[] = {"1234567890-", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_"};
            int yStart = 170;
            for (int r = 0; r < 4; ++r) {
                int len = strlen(rows[r]);
                int xStart = 400 - (len * 45) / 2;
                for (int c = 0; c < len; ++c) {
                    if (hitBox({xStart + c * 45, yStart + r * 50, 40, 40}, tx, ty)) {
                        if (g_kbdInput.length() < 24) g_kbdInput += rows[r][c];
                        handled = true; break;
                    }
                }
                if (handled) break;
            }
            if (!handled) {
                if (hitBox({630, 170, 80, 40}, tx, ty)) {
                    if (!g_kbdInput.empty()) g_kbdInput.pop_back();
                } else if (hitBox({340, 380, 120, 45}, tx, ty)) {
                    if (g_kbdInput.length() < 24) g_kbdInput += " ";
                } else if (hitBox({200, 380, 120, 45}, tx, ty)) {
                    g_modalState = MODAL_NONE;
                } else if (hitBox({480, 380, 120, 45}, tx, ty)) {
                    if (g_kbdInput.empty()) g_kbdInput = "Untitled";
                    std::string fname = "/" + g_kbdInput + ".mhb";
                    std::replace(fname.begin(), fname.end(), ' ', '_');
                    saveBankToFile(fname.c_str(), engine.getSelectedTala(), engine.getSelectedSong());
                    g_modalState = MODAL_NONE;
                }
            }
            redrawEntireScreen();
        } else if (g_modalState != MODAL_NONE) {
            const char** list = nullptr;
            int count = 0;
            if (g_modalState == MODAL_BOL) { list = VALID_BOLS; count = NUM_VALID_BOLS; }
            else if (g_modalState == MODAL_KARTAL) { list = VALID_KARTALS; count = NUM_VALID_KARTALS; }
            else if (g_modalState == MODAL_SWARA) { list = VALID_SWARAS; count = NUM_VALID_SWARAS; }

            int cols = 6;
            int cellW = 100, cellH = 45;
            int startX = 400 - (cols * (cellW + 10)) / 2;
            int startY = 80;
            bool selectionMade = false;

            for (int i = 0; i < count; ++i) {
                TouchButton btn = {startX + (i % cols) * (cellW + 10), startY + (i / cols) * (cellH + 10), cellW, cellH};
                if (hitBox(btn, tx, ty)) {
                    if (g_currentView == VIEW_TALA_ED) {
                        auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[engine.getSelectedTala()];
                        if (g_modalState == MODAL_BOL) {
                            while (kt.bolCells.size() <= static_cast<size_t>(g_editingCellIdx)) kt.bolCells.push_back("-");
                            kt.bolCells[g_editingCellIdx] = list[i];
                        } else if (g_modalState == MODAL_KARTAL) {
                            while (kt.kartalCells.size() <= static_cast<size_t>(g_editingCellIdx)) kt.kartalCells.push_back("-");
                            kt.kartalCells[g_editingCellIdx] = list[i];
                        }
                    } else if (g_currentView == VIEW_HARM_ED) {
                        auto& hs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs())[engine.getSelectedSong()];
                        if (!hs.forms.empty()) {
                            while (hs.forms[0].swaraCells.size() <= static_cast<size_t>(g_editingCellIdx)) hs.forms[0].swaraCells.push_back("-");
                            hs.forms[0].swaraCells[g_editingCellIdx] = list[i];
                        }
                    }
                    selectionMade = true;
                    break;
                }
            }

            g_modalState = MODAL_NONE;
            if (selectionMade) engine.rebuildActivePatterns();
            redrawEntireScreen();

        } else if (hitBox(BTN_LANG, tx, ty)) {
            g_langIdx = (g_langIdx + 1) % static_cast<int>(I18n::getLanguages().size());
            redrawEntireScreen();
        } else if (g_currentView == VIEW_LIVE) {
            // --- LIVE VIEW LOGIC ---
            int pianoMidi = hitTestPiano(tx, ty);
            if (pianoMidi >= 0) {
                if (pianoMidi != g_touchHeldMidi) {
                    if (g_touchHeldMidi >= 0) engine.noteOff(g_touchHeldMidi);
                    g_touchHeldMidi = pianoMidi;
                    engine.noteOn(g_touchHeldMidi, 105);
                    drawPianoKeyboard(g_touchHeldMidi);
                    drawSwaraPopup();
                }
            } else {
                if (hitBox(TAB_TALA, tx, ty)) { g_currentView = VIEW_TALA_ED; redrawEntireScreen(); }
                else if (hitBox(TAB_HARM, tx, ty)) { g_currentView = VIEW_HARM_ED; redrawEntireScreen(); }
                else if (hitBox(BTN_START, tx, ty)) {
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    else engine.stopPlayback();
                    drawTopTransportRow();
                } else if (hitBox(BTN_PAUSE, tx, ty)) {
                    engine.togglePause();
                    drawTopTransportRow();
                } else if (hitBox(BTN_COUNT, tx, ty)) {
                    g_useCountdown = !g_useCountdown;
                    drawTopTransportRow();
                } else if (hitBox(BTN_TALA_PREV, tx, ty)) {
                    engine.selectTala(engine.getSelectedTala() - 1);
                    drawTopTransportRow();
                    drawFooter();
                } else if (hitBox(BTN_TALA_NEXT, tx, ty)) {
                    engine.selectTala(engine.getSelectedTala() + 1);
                    drawTopTransportRow();
                    drawFooter();
                } else if (hitBox(BTN_SONG_PREV, tx, ty)) {
                    engine.selectHarmoniumSong(engine.getSelectedSong() - 1);
                    drawTopTransportRow();
                    drawUtilityRow();
                    drawTrackButtons();
                    drawFooter();
                } else if (hitBox(BTN_SONG_NEXT, tx, ty)) {
                    engine.selectHarmoniumSong(engine.getSelectedSong() + 1);
                    drawTopTransportRow();
                    drawUtilityRow();
                    drawTrackButtons();
                    drawFooter();
                } else if (hitBox(BTN_BPM_DOWN, tx, ty)) {
                    engine.setBpm(engine.getBpm() - 5);
                    drawTopTransportRow();
                } else if (hitBox(BTN_BPM_UP, tx, ty)) {
                    engine.setBpm(engine.getBpm() + 5);
                    drawTopTransportRow();
                } else if (hitBox(BTN_SA_CYCLE, tx, ty)) {
                    engine.setSaSemitone(engine.getSaSemitone() + 1);
                    drawDroneAndTunerRow();
                    drawPianoKeyboard(g_touchHeldMidi);
                } else if (hitBox(BTN_DRONE_EN, tx, ty)) {
                    engine.setDroneEnabled(!engine.isDroneEnabled());
                    drawDroneAndTunerRow();
                } else if (hitBox(BTN_DRONE_PRE, tx, ty)) {
                    engine.setDronePreset(engine.getDronePreset() + 1);
                    drawDroneAndTunerRow();
                } else if (hitBox(BTN_DRONE_SUR, tx, ty)) {
                    engine.setDroneSwaraMode(engine.getDroneSwaraMode() + 1);
                    drawDroneAndTunerRow();
                } else if (hitBox(BTN_RND_TALA, tx, ty)) {
                    engine.randomizeTala();
                    engine.rebuildActivePatterns();
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    drawTopTransportRow();
                    drawFooter();
                } else if (hitBox(BTN_RND_SONG, tx, ty)) {
                    engine.randomizeRaga();
                    engine.rebuildActivePatterns();
                    engine.setHarmoniumPresetMode(true);
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    drawHarmoniumDspRow();
                    drawUtilityRow();
                    drawTrackButtons();
                } else if (hitBox(BTN_MIDI_MODE, tx, ty)) {
                    engine.setMidiInMode(engine.getMidiInMode() + 1);
                    drawUtilityRow();
                } else if (engine.hasSingingData() && hitBox(BTN_SING_MODE, tx, ty)) {
                    engine.toggleSingingEnabled();
                    drawUtilityRow();
                    drawTrackButtons();
                } else if (hitBox(BTN_HARM_MODE, tx, ty)) {
                    engine.setHarmoniumPresetMode(!engine.isHarmoniumPresetMode());
                    drawHarmoniumDspRow();
                } else if (hitBox(BTN_OCT_DOWN, tx, ty)) {
                    engine.setOctaveShift(engine.getOctaveShift() - 1);
                    drawHarmoniumDspRow();
                } else if (hitBox(BTN_OCT_UP, tx, ty)) {
                    engine.setOctaveShift(engine.getOctaveShift() + 1);
                    drawHarmoniumDspRow();
                } else if (hitBox(BTN_TF_EN, tx, ty)) {
                    engine.setTfEnabled(!engine.isTfEnabled());
                    drawHarmoniumDspRow();
                } else if (hitBox(BTN_EDIT_MODE, tx, ty)) {
                    g_currentView = VIEW_TALA_ED;
                    redrawEntireScreen();
                } else {
                    for (int i = 0; i < SynthEngine::NUM_TRACKS; ++i) {
                        if (i == SynthEngine::TRACK_SINGING && !engine.hasSingingData()) continue;
                        if (hitBox(getTrackButtonRect(i), tx, ty)) {
                            engine.toggleTrackMuted(i);
                            drawTrackButtons();
                            if (i == SynthEngine::TRACK_SINGING) {
                                drawUtilityRow();
                            }
                            break;
                        }
                    }
                }
            }
        } else if (g_currentView == VIEW_TALA_ED || g_currentView == VIEW_HARM_ED) {

            if (hitBox(TAB_LIVE, tx, ty)) { g_currentView = VIEW_LIVE; redrawEntireScreen(); }
            else if (hitBox(TAB_TALA, tx, ty)) { g_currentView = VIEW_TALA_ED; redrawEntireScreen(); }
            else if (hitBox(TAB_HARM, tx, ty)) { g_currentView = VIEW_HARM_ED; redrawEntireScreen(); }

            else if (hitBox(BTN_ED_PREV, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.selectTala(engine.getSelectedTala() - 1);
                else engine.selectHarmoniumSong(engine.getSelectedSong() - 1);
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_NEXT, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.selectTala(engine.getSelectedTala() + 1);
                else engine.selectHarmoniumSong(engine.getSelectedSong() + 1);
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_RAND, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.randomizeTala();
                else engine.randomizeRaga();
                engine.rebuildActivePatterns();
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_PLAY, tx, ty)) {
                if (engine.isPlaying()) engine.stopPlayback();
                else {
                    if (g_currentView == VIEW_HARM_ED) engine.setHarmoniumPresetMode(true);
                    engine.startWithCountdown(false);
                }
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_SAVE, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) g_kbdInput = "Tala_" + std::string(BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name);
                else g_kbdInput = "Raga_" + std::string(BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title);
                std::replace(g_kbdInput.begin(), g_kbdInput.end(), ' ', '_');
                if (g_kbdInput.length() > 20) g_kbdInput = g_kbdInput.substr(0, 20);
                g_modalState = MODAL_SAVE_KBD;
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_LOAD, tx, ty)) {
                g_fileList = listBankFiles();
                g_modalState = MODAL_FILE_LOAD;
                redrawEntireScreen();
            } else if (hitBox(BTN_ED_DIVS, tx, ty)) {
                auto cycleDivs = [](const char* cur) {
                    for (int i = 0; i < 6; i++) {
                        if (strcmp(cur, VALID_DIVISIONS[i]) == 0) return VALID_DIVISIONS[(i + 1) % 6];
                    }
                    return VALID_DIVISIONS[0];
                };
                if (g_currentView == VIEW_TALA_ED) {
                    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[engine.getSelectedTala()];
                    kt.divisions = cycleDivs(kt.divisions);
                } else {
                    auto& hs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs())[engine.getSelectedSong()];
                    hs.divisions = cycleDivs(hs.divisions);
                }
                engine.rebuildActivePatterns();
                redrawEntireScreen();
            } else {
                if (g_currentView == VIEW_TALA_ED) {
                    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[engine.getSelectedTala()];
                    auto vibhags = parseVibhags(kt.divisions);
                    int globalMatra = 0;
                    for (size_t v = 0; v < vibhags.size(); ++v) {
                        for (int m = 0; m < vibhags[v]; ++m) {
                            int r = globalMatra / 8;
                            int c = globalMatra % 8;
                            TouchButton bolBtn = {16 + c * 96, 130 + r * 120 + 28, 90, 36};
                            TouchButton karBtn = {16 + c * 96, 130 + r * 120 + 68, 90, 36};

                            if (hitBox(bolBtn, tx, ty)) {
                                g_editingCellIdx = globalMatra;
                                g_modalState = MODAL_BOL;
                                drawModalSelector();
                                wasTouching = true;
                                return;
                            } else if (hitBox(karBtn, tx, ty)) {
                                g_editingCellIdx = globalMatra;
                                g_modalState = MODAL_KARTAL;
                                drawModalSelector();
                                wasTouching = true;
                                return;
                            }
                            globalMatra++;
                        }
                    }
                } else if (g_currentView == VIEW_HARM_ED) {
                    auto& hs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs())[engine.getSelectedSong()];
                    if (!hs.forms.empty()) {
                        auto vibhags = parseVibhags(hs.divisions);
                        int totalMatras = 0;
                        for (int v : vibhags) totalMatras += v;
                        int cellW = (totalMatras > 0) ? (768 / totalMatras) : 48;
                        if (cellW > 64) cellW = 64;

                        for (int row = 0; row < hs.forms[0].numRows; ++row) {
                            for (int col = 0; col < totalMatras; ++col) {
                                TouchButton sBtn = {16 + col * cellW, 160 + row * 42, cellW - 2, 38};
                                if (hitBox(sBtn, tx, ty)) {
                                    g_editingCellIdx = row * totalMatras + col;
                                    g_modalState = MODAL_SWARA;
                                    drawModalSelector();
                                    wasTouching = true;
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (touching && g_modalState == MODAL_NONE && g_currentView == VIEW_LIVE) {
        int pianoMidi = hitTestPiano(tx, ty);
        if (pianoMidi >= 0) {
            if (pianoMidi != g_touchHeldMidi) {
                if (g_touchHeldMidi >= 0) engine.noteOff(g_touchHeldMidi);
                g_touchHeldMidi = pianoMidi;
                engine.noteOn(g_touchHeldMidi, 105);
                drawPianoKeyboard(g_touchHeldMidi);
                drawSwaraPopup();
            }
        } else if (g_touchHeldMidi >= 0) {
            engine.noteOff(g_touchHeldMidi);
            g_touchHeldMidi = -1;
            drawPianoKeyboard(-1);
        }
    } else if (!touching && wasTouching && g_touchHeldMidi >= 0) {
        engine.noteOff(g_touchHeldMidi);
        g_touchHeldMidi = -1;
        if (g_currentView == VIEW_LIVE) drawPianoKeyboard(-1);
    }

    wasTouching = touching;

    if (g_currentView == VIEW_LIVE && g_modalState == MODAL_NONE) {
        int curMidi = engine.getDetectedMidiNote();
        if (curMidi != g_lastDisplayedMidi || (engine.isPlaying() && engine.getCountdownBeats() > 0)) {
            g_lastDisplayedMidi = curMidi;
            drawSwaraPopup();
        }

#ifdef ESP32
        g_midiConnected = usbMidi.isConnected();
        static bool s_lastUsbAud = false;
        bool curUsbAud = g_usbAudio.isConnected();
        if (curUsbAud != s_lastUsbAud) {
            s_lastUsbAud = curUsbAud;
            drawTabs();
        }
#endif

        int curMatra = engine.getCurrentMatra();
        if (curMatra != g_lastDisplayedMatra || g_midiConnected != g_lastMidiConnected) {
            g_lastDisplayedMatra = curMatra;
            g_lastMidiConnected = g_midiConnected;

            drawTalometerLEDs();
            drawFooter();
        }
    }
}

#ifdef ESP32

static void initI2S() {
    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg.sample_rate = AUDIO_SRATE;
    i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg.dma_buf_count = 6;
    i2s_cfg.dma_buf_len = 256;
    i2s_cfg.use_apll = false;
    i2s_cfg.tx_desc_auto_clear = true;

    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num = I2S_BCLK_PIN;
    pin_cfg.ws_io_num = I2S_LRCK_PIN;
    pin_cfg.data_out_num = I2S_DOUT_PIN;
    pin_cfg.data_in_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// Master Audio Task routes audio intelligently to avoid duplicating the engine state pull
static void masterAudioTask(void*) {
    float monoBuf[256];
    int16_t stereoBuf[512];

    while (true) {
        engine.renderFramesMono(monoBuf, 256);

        WifiAudioStreamer::instance().pushMonoSamples(monoBuf, 256);

        for (int i = 0; i < 256; ++i) {
            int16_t s = static_cast<int16_t>(monoBuf[i] * 32767.0f);
            stereoBuf[i * 2]     = s;
            stereoBuf[i * 2 + 1] = s;
        }

        if (g_audioOutMode == 0 && g_usbAudio.isConnected()) {
            g_usbAudio.writePcm(stereoBuf, sizeof(stereoBuf));
        } else {
            size_t bytesWritten = 0;
            i2s_write(I2S_NUM_0, stereoBuf, sizeof(stereoBuf), &bytesWritten, portMAX_DELAY);
        }

        // Always yield so loop() on Core 1 can process touch events and release m_mutex
        vTaskDelay(1);
    }

}

void setup() {
    Serial.begin(115200);
    initFileSystem();
    BtAudioManager::instance().begin();
    int dummyTala = 0, dummySong = 0;
    loadBankFromFile("/Default_Bank.mhb", dummyTala, dummySong);

    // Start Wi-Fi SoftAP BEFORE g_display.init() with NVS flash persistence disabled
    // so SPI flash writes never collide with the BufferedRGBBus PSRAM bounce-buffer ISR
    WiFi.persistent(false);
    WifiAudioStreamer::instance().begin(&engine);

    initWaveshare7InchBacklightAndTouch();
    g_display.init();
    g_display.setRotation(0);
    g_display.setAttribute(UTF8_SWITCH, true);
    g_display.fillScreen(rgb565(22, 25, 30));
    g_display.setFont(&fonts::Font2);

    g_display.setTextDatum(MC_DATUM);
    g_display.setTextColor(rgb565(0, 229, 255));
    g_display.drawString("Synthesizing Xpressive Percussion Voices into PSRAM...", 400, 240);

    engine.init(AUDIO_SRATE);
    initI2S();

    // Initialize USB Audio Out and USB Host stack
    g_usbAudio.begin();
    usbMidi.begin([](int note, int vel, bool on) {
        engine.handleIncomingMidi(note, vel, on);
    });

    // Draw the full UI before starting the real-time audio task on Core 1
    redrawEntireScreen();

    // Run audio synthesis at priority 5 so it never starves Core 1 UI/touch tasks
    xTaskCreatePinnedToCore(masterAudioTask, "SynthCore1", 8192, nullptr, 2, nullptr, 1);
}

void loop() {
    esp32LoopTick();
    WifiAudioStreamer::instance().handle();
    delay(15);
}

#else // Qt Desktop Simulator

class QtAudioBridge : public QIODevice {
public:
    explicit QtAudioBridge(QObject* parent = nullptr) : QIODevice(parent) {
        m_format.setSampleRate(22050);
        m_format.setChannelCount(2);
        m_format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (!dev.isFormatSupported(m_format)) {
            m_format = dev.preferredFormat();
        }
        engine.init(static_cast<float>(m_format.sampleRate()));

        m_sink = new QAudioSink(dev, m_format, this);
        m_sink->setBufferSize(8192);
        open(QIODevice::ReadOnly);
        m_sink->start(this);
    }

    ~QtAudioBridge() override {
        if (m_sink) m_sink->stop();
        close();
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        return (m_sink ? m_sink->bufferSize() : 0) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        std::memset(data, 0, maxlen);
        int channels = std::max(1, m_format.channelCount());
        bool isFloat = (m_format.sampleFormat() == QAudioFormat::Float);
        int bytesPerSample = isFloat ? sizeof(float) : sizeof(qint16);
        int frames = static_cast<int>(maxlen) / (bytesPerSample * channels);
        if (frames <= 0) return maxlen;

        m_monoScratch.resize(frames);
        engine.renderFramesMono(m_monoScratch.data(), frames);

        if (isFloat) {
            float* outF = reinterpret_cast<float*>(data);
            for (int i = 0; i < frames; ++i) {
                for (int c = 0; c < channels; ++c) outF[i * channels + c] = m_monoScratch[i];
            }
        } else {
            qint16* outI = reinterpret_cast<qint16*>(data);
            for (int i = 0; i < frames; ++i) {
                qint16 s = static_cast<qint16>(m_monoScratch[i] * 32767.0f);
                for (int c = 0; c < channels; ++c) outI[i * channels + c] = s;
            }
        }
        return maxlen;
    }

    qint64 writeData(const char*, qint64 len) override { return len; }

private:
    QAudioFormat m_format;
    QAudioSink* m_sink = nullptr;
    std::vector<float> m_monoScratch;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    Waveshare7QtDisplay display;
    g_tft = &display;

    QtAudioBridge audioBridge(&display);

    if (audioBridge.findChild<QAudioSink*>()) {
        audioBridge.findChild<QAudioSink*>()->setBufferSize(2048);
    }

    display.onPcKeyMidi = [](int midi, bool on) {
        engine.handleIncomingMidi(midi, 105, on);
        if (g_currentView == VIEW_LIVE && g_modalState == MODAL_NONE && (engine.getMidiInMode() == 0 || engine.getMidiInMode() == 2 || (engine.getMidiInMode() == 1 && midi >= 52))) {
            if (on) {
                drawPianoKeyboard(midi);
                drawSwaraPopup();
            } else {
                drawPianoKeyboard(-1);
            }
        }
    };

    MidiReader midiReader([&display](int note, int vel, bool on) {
        engine.handleIncomingMidi(note, vel, on);

        QMetaObject::invokeMethod(&display, [note, vel, on]() {
            if (g_currentView == VIEW_LIVE && g_modalState == MODAL_NONE && (engine.getMidiInMode() == 0 || engine.getMidiInMode() == 2 || (engine.getMidiInMode() == 1 && note >= 52))) {
                if (on) {
                    drawPianoKeyboard(note);
                    drawSwaraPopup();
                } else {
                    drawPianoKeyboard(-1);
                }
            }
        }, Qt::QueuedConnection);
    }, &display);

    QObject::connect(&midiReader, &MidiReader::midiStatusChanged, &display, [](const QString& status) {
        g_midiConnected = status.startsWith("MIDI Connected");
    });
    midiReader.start();

    BtAudioManager::instance().begin();
    redrawEntireScreen();
    display.show();

    QTimer loopTimer(&display);
    QObject::connect(&loopTimer, &QTimer::timeout, &display, &esp32LoopTick);
    loopTimer.start(16);

    int ret = app.exec();
    midiReader.stopReader();
    midiReader.wait();
    return ret;
}
#endif
