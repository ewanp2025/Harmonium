#include "bt_audio.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <atomic>
#include <cctype>

#include "i18n.h"
#include "synthengine.h"

#ifdef ESP32
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "display_driver.h"
#include "usb_midi_host.h"
#include "storage.h"
#include "usb_audio.h"
#include "wifi_stream.h"

#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN  6
#endif
#ifndef I2S_DOUT_PIN
#define I2S_DOUT_PIN  15
#endif
#ifndef I2S_LRCK_PIN
#define I2S_LRCK_PIN  16
#endif
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN   8
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN   9
#endif
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

#define I2S_BCLK_PIN  6
#define I2S_DOUT_PIN  15
#define I2S_LRCK_PIN  16
#define I2C_SDA_PIN   8
#define I2C_SCL_PIN   9
#define AUDIO_SRATE   22050

static Waveshare7QtDisplay* g_tft = nullptr;

inline void initFileSystem() {}
inline std::vector<std::string> listBankFiles() { return {"Default_Bank.mhb"}; }
inline bool saveBankToFile(const char*, int, int) { return true; }
inline bool loadBankFromFile(const char*, int&, int&) { return true; }
#endif

static SynthEngine engine;
static bool g_midiConnected = false;

static const char* WESTERN_NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};


// APPLICATION & MODAL STATE


enum AppView { VIEW_LIVE, VIEW_TALA_ED, VIEW_HARM_ED, VIEW_INST_TEST };
static AppView g_currentView = VIEW_LIVE;

enum ModalState {
    MODAL_NONE,
    MODAL_BOL,
    MODAL_KARTAL,
    MODAL_SWARA,
    MODAL_FILE_LOAD,
    MODAL_SAVE_KBD,
    MODAL_GPIO_CFG,
    MODAL_HARM_CFG
};
static ModalState g_modalState = MODAL_NONE;
static int g_editingCellIdx = -1;
static std::vector<std::string> g_fileList;
static std::string g_kbdInput;
static int g_audioOutMode = 1; // 0 = USB, 1 = I2S, 2 = WIFI

static int g_harmPreviewTicks = 0;
static int g_harmPreviewMidi  = -1;

// Instrument Test Screen State & Transient Capture Buffer (Zero overhead when inactive)
static int   g_testSelectedInst = 0;
static bool  g_testAutoCycle    = false;
static int   g_testAutoTicks    = 0;
static int   g_testFrameDiv     = 0;
static float g_specPeakHold[32] = {0.0f};

static float             g_testCapturedWave[SynthEngine::SCOPE_RING_SIZE] = {0.0f};
static std::atomic<int>  g_testCaptureWriteIdx{512}; // < 512 means armed/capturing
static std::atomic<bool> g_testCaptureTriggered{false};
static float             g_testVisualDecay = 0.0f;

static inline void armTestTransientCapture() {
    g_testCaptureTriggered.store(false, std::memory_order_relaxed);
    g_testCaptureWriteIdx.store(0, std::memory_order_release);
    g_testVisualDecay = 1.0f;
}

static inline void captureTestAudioFrames(const float* monoBuf, int frames) {
    int wIdx = g_testCaptureWriteIdx.load(std::memory_order_acquire);
    if (wIdx >= SynthEngine::SCOPE_RING_SIZE) return;

    bool trig = g_testCaptureTriggered.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        float s = monoBuf[i];
        if (!trig) {
            if (std::fabs(s) > 0.0015f) {
                trig = true;
            } else {
                continue;
            }
        }
        g_testCapturedWave[wIdx++] = s;
        if (wIdx >= SynthEngine::SCOPE_RING_SIZE) break;
    }
    g_testCaptureTriggered.store(trig, std::memory_order_relaxed);
    g_testCaptureWriteIdx.store(wIdx, std::memory_order_release);
}

static void triggerAndCaptureTestInstrument(int trackIdx) {
    armTestTransientCapture();
    engine.triggerTestInstrument(trackIdx);
}

struct BoardGpioConfig {
    int i2sBclk   = I2S_BCLK_PIN;
    int i2sLrck   = I2S_LRCK_PIN;
    int i2sDout   = I2S_DOUT_PIN;
    int i2cSda    = I2C_SDA_PIN;
    int i2cScl    = I2C_SCL_PIN;
    int belPosPin = -1; // -1 = OFF, or ADC GPIO pin
    int belDepPin = -1; // -1 = derive pumping speed from POS pin, or 2nd ADC GPIO pin
};
static BoardGpioConfig g_pins;
static float g_prevPosNorm = 0.5f;
static float g_smoothedVelDep = 0.8f;

static void applyI2SPins() {
#ifdef ESP32
    i2s_pin_config_t pin_cfg = {};
    pin_cfg.bck_io_num   = g_pins.i2sBclk;
    pin_cfg.ws_io_num    = g_pins.i2sLrck;
    pin_cfg.data_out_num = g_pins.i2sDout;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
#endif
}

static void applyI2CPins() {
#if defined(ESP32) && (SCREEN_PROFILE != 0)
    if (g_pins.i2cSda >= 0 && g_pins.i2cScl >= 0) {
        Wire.end();
        Wire.begin(g_pins.i2cSda, g_pins.i2cScl, 400000);
    }
#endif
}

static void saveGpioConfig() {
#ifdef ESP32
    File f = LittleFS.open("/gpio_cfg.json", "w");
    if (f) {
        JsonDocument doc;
        doc["bclk"]   = g_pins.i2sBclk;
        doc["lrck"]   = g_pins.i2sLrck;
        doc["dout"]   = g_pins.i2sDout;
        doc["sda"]    = g_pins.i2cSda;
        doc["scl"]    = g_pins.i2cScl;
        doc["belPos"] = g_pins.belPosPin;
        doc["belDep"] = g_pins.belDepPin;
        serializeJson(doc, f);
        f.close();
    }
    if (g_pins.belPosPin >= 0) pinMode(g_pins.belPosPin, INPUT);
    if (g_pins.belDepPin >= 0) pinMode(g_pins.belDepPin, INPUT);
#endif
    applyI2SPins();
    applyI2CPins();
}

static void loadGpioConfig() {
#ifdef ESP32
    File f = LittleFS.open("/gpio_cfg.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        g_pins.i2sBclk   = doc["bclk"]   | I2S_BCLK_PIN;
        g_pins.i2sLrck   = doc["lrck"]   | I2S_LRCK_PIN;
        g_pins.i2sDout   = doc["dout"]   | I2S_DOUT_PIN;
        g_pins.i2cSda    = doc["sda"]    | I2C_SDA_PIN;
        g_pins.i2cScl    = doc["scl"]    | I2C_SCL_PIN;
        g_pins.belPosPin = doc["belPos"] | -1;
        g_pins.belDepPin = doc["belDep"] | -1;
    }
    f.close();
    if (g_pins.belPosPin >= 0) pinMode(g_pins.belPosPin, INPUT);
    if (g_pins.belDepPin >= 0) pinMode(g_pins.belDepPin, INPUT);
#endif
}

static void saveHarmoniumConfig() {
    const auto& p = engine.getHarmoniumParams();
#ifdef ESP32
    File f = LittleFS.open("/harm_cfg.json", "w");
    if (f) {
        JsonDocument doc;
        doc["bass"]      = p.bassStop;
        doc["principal"] = p.principalStop;
        doc["celeste"]   = p.celesteStop;
        doc["coupler"]   = p.couplerStop;
        doc["cents"]     = p.celesteCents;
        doc["bright"]    = p.brightness;
        doc["atk"]       = p.attackRate;
        doc["rel"]       = p.releaseRate;
        doc["pump"]      = p.pumpModDepth;
        serializeJson(doc, f);
        f.close();
    }
#else
    FILE* fp = std::fopen("harm_cfg.json", "w");
    if (fp) {
        std::fprintf(fp, "%.3f %.3f %.3f %.3f %.2f %.3f %.2f %.2f %.3f\n",
                     p.bassStop, p.principalStop, p.celesteStop, p.couplerStop,
                     p.celesteCents, p.brightness, p.attackRate, p.releaseRate, p.pumpModDepth);
        std::fclose(fp);
    }
#endif
}

static void loadHarmoniumConfig() {
    HarmoniumParams p = engine.getHarmoniumParams();
#ifdef ESP32
    File f = LittleFS.open("/harm_cfg.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
        p.bassStop      = doc["bass"]      | 0.65f;
        p.principalStop = doc["principal"] | 1.00f;
        p.celesteStop   = doc["celeste"]   | 0.45f;
        p.couplerStop   = doc["coupler"]   | 0.00f;
        p.celesteCents  = doc["cents"]     | 8.0f;
        p.brightness    = doc["bright"]    | 0.75f;
        p.attackRate    = doc["atk"]       | 8.0f;
        p.releaseRate   = doc["rel"]       | 18.0f;
        p.pumpModDepth  = doc["pump"]      | 0.18f;
        engine.applyHarmoniumParams(p);
    }
    f.close();
#else
    FILE* fp = std::fopen("harm_cfg.json", "r");
    if (fp) {
        if (std::fscanf(fp, "%f %f %f %f %f %f %f %f %f",
                        &p.bassStop, &p.principalStop, &p.celesteStop, &p.couplerStop,
                        &p.celesteCents, &p.brightness, &p.attackRate, &p.releaseRate, &p.pumpModDepth) == 9) {
            engine.applyHarmoniumParams(p);
        }
        std::fclose(fp);
    }
#endif
}

static void eraseHarmoniumConfigToDefault() {
#ifdef ESP32
    LittleFS.remove("/harm_cfg.json");
#else
    std::remove("harm_cfg.json");
#endif
    engine.resetHarmoniumParamsToDefault();
}

static void pollBellowsGpioPins() {
    if (!engine.isBellowsPosManual() && !engine.isBellowsDepManual()) return;
#ifdef ESP32
    float posNorm = g_prevPosNorm;
    if (g_pins.belPosPin >= 0) {
        int rawPos = analogRead(g_pins.belPosPin);
        posNorm = std::clamp(rawPos / 4095.0f, 0.0f, 1.0f);
    }

    float depNorm = 1.0f;
    if (g_pins.belDepPin >= 0) {
        int rawDep = analogRead(g_pins.belDepPin);
        depNorm = std::clamp(rawDep / 4095.0f, 0.0f, 1.2f);
    } else {
        float speed = std::fabs(posNorm - g_prevPosNorm) * 18.0f;
        g_smoothedVelDep = std::clamp(g_smoothedVelDep * 0.92f + speed * 0.65f, 0.15f, 1.2f);
        depNorm = g_smoothedVelDep;
    }
    g_prevPosNorm = posNorm;
    engine.updateBellowsSensors(posNorm, depNorm);
#endif
}

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
    return (b.w > 0 && b.h > 0 && tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h);
}


// MULTI-RESOLUTION SCREEN PROFILE


struct UiLayout {
    int scrW;
    int scrH;
    bool hasVisualPiano;
    bool compactLabels;

    // Top Navigation & Hardware Bar (Includes tabTest)
    TouchButton tabLive, tabTala, tabHarm, tabTest, btnAudio, btnBelPos, btnBelDep, btnGpio, btnLang;

    // Live Transport Row
    TouchButton btnStart, btnPause, btnCount;
    TouchButton btnTalaPrev, talaBox, btnTalaNext;
    TouchButton btnSongPrev, songBox, btnSongNext;
    TouchButton btnBpmDown, bpmBox, btnBpmUp;

    // Swara / Tuner / Drone Row
    TouchButton swaraBox, btnSaCycle, btnDroneEn, btnDronePre, btnDroneSur;

    // Talometer Bar
    int taloY, taloH, taloStepX;

    // 24-Track Grid (6 columns x 4 rows)
    int trkStartX, trkStartY, trkStepX, trkStepY, trkW, trkH;

    // Utility & Harmonium DSP Rows
    TouchButton btnRndTala, btnRndSong, btnMidiMode, btnSingMode;
    TouchButton btnHarmMode, btnOctDown, octLbl, btnOctUp, btnTfEn, btnEditMode;

    int pianoY, pianoH;

    // Footer Bar
    int footerY, footerH;

    // Editor Shared Navigation Row
    TouchButton btnEdPrev, btnEdName, btnEdNext, btnEdNew, btnEdRand, btnEdPlay, btnEdSave;
    TouchButton btnEdDivs, btnEdLoad;
    int edGridY;
};

// Profile 0: 800x480 (7" Waveshare RGB Touch Display with 37-key Touch Piano)
static const UiLayout LAYOUT_800x480 = {
    800, 480, true, false,
    // Top bar
    {  6,  6,  72, 34}, { 81,  6,  94, 34}, {178,  6,  94, 34}, {275,  6,  72, 34},
    {350,  6, 104, 34}, {457,  6,  84, 34}, {544,  6,  84, 34}, {631,  6,  64, 34}, {698,  6,  96, 34},
    // Transport row
    {  6, 46,  86, 34}, { 96, 46,  72, 34}, {172, 46,  58, 34},
    {236, 46,  28, 34}, {266, 46, 156, 34}, {424, 46,  28, 34},
    {458, 46,  28, 34}, {488, 46, 174, 34}, {664, 46,  28, 34},
    {698, 46,  32, 34}, {731, 46,  30, 34}, {762, 46,  32, 34},
    // Drone & tuner row
    {  6, 86, 178, 34}, {190, 86, 155, 34}, {350, 86, 115, 34}, {470, 86, 155, 34}, {630, 86, 164, 34},
    // Talometer
    122, 18, 28,
    // Track buttons (6x4)
    6, 142, 132, 27, 130, 23,
    // Utility row
    {  6, 252, 150, 28}, {162, 252, 150, 28}, {318, 252, 200, 28}, {524, 252, 270, 28},
    // Harmonium DSP row
    {  6, 286, 330, 28}, {342, 286,  56, 28}, {400, 286,  60, 28}, {462, 286,  56, 28}, {524, 286, 130, 28}, {660, 286, 134, 28},
    // Piano
    320, 134,
    // Footer
    456, 24,
    // Editor Nav
    { 16, 46,  40, 34}, { 60, 46, 200, 34}, {264, 46,  40, 34}, {312, 46,  80, 34},
    {400, 46, 110, 34}, {518, 46, 110, 34}, {636, 46, 100, 34},
    { 16, 86, 130, 34}, {636, 86, 100, 34},
    130
};

// Profile 1: 320x240 (2.8" SPI ILI9341/ST7789 Touch Display - Full Features, No Visual Piano)
static const UiLayout LAYOUT_320x240 = {
    320, 240, false, true,
    // Top bar (y=2, h=22)
    {  2,  2,  28, 22}, { 31,  2,  32, 22}, { 64,  2,  32, 22}, { 97,  2,  32, 22},
    {130,  2,  48, 22}, {180,  2,  34, 22}, {216,  2,  34, 22}, {252,  2,  30, 22}, {284,  2,  34, 22},
    // Transport row (y=26, h=22)
    {  2, 26,  36, 22}, { 40, 26,  32, 22}, { 74, 26,  26, 22},
    {102, 26,  14, 22}, {117, 26,  54, 22}, {172, 26,  14, 22},
    {188, 26,  14, 22}, {203, 26,  54, 22}, {258, 26,  14, 22},
    {274, 26,  13, 22}, {288, 26,  18, 22}, {307, 26,  11, 22},
    // Drone & tuner row (y=50, h=22)
    {  2, 50,  72, 22}, { 76, 50,  64, 22}, {142, 50,  52, 22}, {196, 50,  60, 22}, {258, 50,  60, 22},
    // Talometer (y=74, h=12)
    74, 12, 16,
    // Track buttons (6x4 grid, y=88..163)
    2, 88, 53, 19, 51, 17,
    // Utility row (y=166, h=22)
    {  2, 166,  62, 22}, { 66, 166,  62, 22}, {130, 166,  92, 22}, {224, 166,  94, 22},
    // Harmonium DSP row (y=190, h=22)
    {  2, 190, 118, 22}, {122, 190,  26, 22}, {150, 190,  34, 22}, {186, 190,  26, 22}, {214, 190,  50, 22}, {266, 190,  52, 22},
    // Piano (hidden visually on 320x240)
    240, 0,
    // Footer (y=216, h=24)
    216, 24,
    // Editor Nav (y=26 & y=50)
    {  2, 26,  20, 22}, { 24, 26,  96, 22}, {122, 26,  20, 22}, {144, 26,  38, 22},
    {184, 26,  44, 22}, {230, 26,  44, 22}, {276, 26,  42, 22},
    {  2, 50,  90, 20}, {230, 50,  88, 20},
    74
};

// Profile 2: 128x64 (Monochrome LCD Keyboard Box Preview)
static const UiLayout LAYOUT_128x64 = {
    128, 64, false, true,
    { 1,  1, 20, 11}, {22,  1, 20, 11}, {43,  1, 20, 11}, {64,  1, 20, 11},
    { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0}, {85,  1, 20, 11}, {106, 1, 21, 11},
    { 1, 13, 30, 12}, {32, 13, 24, 12}, { 0,  0,  0,  0},
    {57, 13, 10, 12}, {68, 13, 48, 12}, {117,13, 10, 12},
    { 1, 26, 10, 12}, {12, 26, 62, 12}, {75, 26, 10, 12},
    {86, 26, 12, 12}, {99, 26, 16, 12}, {116,26, 11, 12},
    { 1, 39, 52, 12}, {54, 39, 36, 12}, {91, 39, 36, 12}, { 0,  0,  0,  0}, { 0,  0,  0,  0},
    0, 0, 6,
    0, 0, 0, 0, 0, 0,
    { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0},
    { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0}, { 0,  0,  0,  0},
    64, 0,
    52, 12,
    { 1, 13, 12, 11}, {14, 13, 52, 11}, {67, 13, 12, 11}, { 0,  0,  0,  0},
    {80, 13, 22, 11}, {103,13, 24, 11}, { 0,  0,  0,  0},
    { 1, 25, 60, 11}, {65, 25, 62, 11},
    37
};

static int getActiveProfileIdx() {
#ifdef ESP32
    return SCREEN_PROFILE;
#else
    return g_tft ? g_tft->currentProfile() : 0;
#endif
}

static const UiLayout& UI() {
    int p = getActiveProfileIdx();
    if (p == 1) return LAYOUT_320x240;
    if (p == 2) return LAYOUT_128x64;
    return LAYOUT_800x480;
}



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

static std::string clipText(const std::string& s, size_t maxChars) {
    if (s.length() <= maxChars) return s;
    return s.substr(0, maxChars);
}


// TOUCH PIANO LOGIC


static const int PIANO_START_MIDI = 48;
static const int PIANO_NUM_KEYS   = 37;

static inline bool isBlackKey(int midi) {
    int n = midi % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

static int hitTestPiano(int tx, int ty) {
    const auto& u = UI();
    if (!u.hasVisualPiano || u.pianoH <= 0) return -1;
    if (ty < u.pianoY || ty >= u.pianoY + u.pianoH) return -1;

    int whiteCount = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        if (!isBlackKey(PIANO_START_MIDI + i)) whiteCount++;
    }
    float whiteW = static_cast<float>(u.scrW) / whiteCount;
    int blackH = static_cast<int>(u.pianoH * 0.60f);

    if (ty - u.pianoY <= blackH) {
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
    if (b.w <= 0 || b.h <= 0) return;
    int r = (b.h >= 24) ? 4 : 2;
    g_tft->fillRoundRect(b.x, b.y, b.w, b.h, r, bgCol);
    g_tft->drawRoundRect(b.x, b.y, b.w, b.h, r, borderCol);
    g_tft->setTextColor(textCol, bgCol);
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

static void drawTabs() {
    const auto& u = UI();
    const bool compact = u.compactLabels;

    drawButton(u.tabLive, compact ? "LIV" : "<- LIVE",
               g_currentView == VIEW_LIVE ? rgb565(0, 105, 92) : rgb565(30, 40, 50));
    drawButton(u.tabTala, compact ? (g_langIdx == 1 ? "ताल" : "TAL") : (g_langIdx == 1 ? "ताल ठेका" : "TALA ED"),
               g_currentView == VIEW_TALA_ED ? rgb565(0, 105, 92) : rgb565(30, 40, 50));
    drawButton(u.tabHarm, compact ? (g_langIdx == 1 ? "राग" : "RAG") : (g_langIdx == 1 ? "स्वरलिपि" : "HARM ED"),
               g_currentView == VIEW_HARM_ED ? rgb565(0, 105, 92) : rgb565(30, 40, 50));
    drawButton(u.tabTest, compact ? "TST" : "TEST LAB",
               g_currentView == VIEW_INST_TEST ? rgb565(194, 24, 91) : rgb565(45, 30, 60),
               rgb565(255, 202, 40), 0xFFFF);

#ifdef ESP32
    const char* fullAud[]  = {g_usbAudio.getStatusLabel(), "OUT: I2S", "OUT: WIFI"};
    const char* shortAud[] = {g_usbAudio.isConnected() ? "USB:OK" : "USB", "I2S", "WIFI"};
    uint16_t audBg = (g_audioOutMode == 0 && g_usbAudio.isConnected())
                         ? rgb565(27, 94, 32)
                         : rgb565(37, 40, 48);
    drawButton(u.btnAudio, compact ? shortAud[g_audioOutMode] : fullAud[g_audioOutMode],
               audBg, rgb565(0, 229, 255), 0xFFFF);
#else
    drawButton(u.btnAudio, compact ? "QT PC" : "OUT: QT PC",
               rgb565(37, 40, 48), rgb565(0, 229, 255), 0xFFFF);
#endif

    bool posMan = engine.isBellowsPosManual();
    drawButton(u.btnBelPos,
               compact ? (posMan ? "P:M" : "P:A") : (posMan ? "POS:MAN" : "POS:AUTO"),
               posMan ? rgb565(230, 81, 0) : rgb565(37, 40, 48), rgb565(255, 202, 40), 0xFFFF);

    bool depMan = engine.isBellowsDepManual();
    drawButton(u.btnBelDep,
               compact ? (depMan ? "D:M" : "D:A") : (depMan ? "DEP:MAN" : "DEP:AUTO"),
               depMan ? rgb565(230, 81, 0) : rgb565(37, 40, 48), rgb565(255, 202, 40), 0xFFFF);

    drawButton(u.btnGpio, compact ? "PIN" : "PINS", rgb565(55, 71, 79), rgb565(0, 229, 255), 0xFFFF);
    drawButton(u.btnLang, compact ? (g_langIdx == 1 ? "हिन्दी" : "EN") : L().menuLabel,
               rgb565(30, 41, 59), rgb565(0, 229, 255), rgb565(0, 229, 255));
}

static void drawEditorNavRow(const char* name, const char* divisions) {
    const auto& u = UI();
    const auto& lang = L();
    const bool compact = u.compactLabels;

    drawButton(u.btnEdPrev, "<", rgb565(74, 20, 140));

    std::string shortName = clipText(name ? name : "", compact ? 11 : 18);
    drawButton(u.btnEdName, shortName.c_str(), rgb565(37, 40, 48), rgb565(255, 202, 40));
    drawButton(u.btnEdNext, ">", rgb565(74, 20, 140));

    drawButton(u.btnEdNew, compact ? "+New" : lang.btnEdNew, rgb565(0, 105, 92));
    drawButton(u.btnEdRand, compact ? "Rnd" : lang.btnEdRand, rgb565(106, 27, 154), rgb565(255, 235, 59));

    bool playing = engine.isPlaying();
    drawButton(u.btnEdPlay, compact ? (playing ? "Stop" : "Play") : lang.btnEdPlay,
               playing ? rgb565(183, 28, 28) : rgb565(27, 94, 32));
    drawButton(u.btnEdSave, compact ? "Save" : lang.btnEdSave, rgb565(46, 125, 50));

    std::string divStr = std::string(compact ? "D:" : lang.lblEdDivisions) + (divisions ? divisions : "4+4");
    drawButton(u.btnEdDivs, divStr.c_str(), rgb565(38, 50, 56), rgb565(0, 172, 193));
    drawButton(u.btnEdLoad, compact ? "Load" : lang.btnEdLoad, rgb565(2, 119, 189));
}

static void drawFileSelector() {
    const auto& u = UI();
    int padX = u.compactLabels ? 6 : 40;
    int padY = u.compactLabels ? 24 : 30;
    int mw = u.scrW - padX * 2;
    int mh = u.scrH - padY - 6;

    g_tft->fillRect(padX, padY, mw, mh, rgb565(15, 20, 25));
    g_tft->drawRect(padX, padY, mw, mh, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(u.compactLabels ? "LOAD .MHB BANK (Tap out to exit)"
                                      : "SELECT A .MHB BANK FILE TO LOAD (Tap outside to cancel)",
                      u.scrW / 2, padY + (u.compactLabels ? 12 : 20));

    int cols = 3;
    int cellW = (mw - 24) / cols;
    int cellH = u.compactLabels ? 28 : 45;
    int stepY = u.compactLabels ? 34 : 60;
    int startY = padY + (u.compactLabels ? 26 : 70);

    for (size_t i = 0; i < g_fileList.size() && i < 12; ++i) {
        TouchButton btn = {
            padX + 8 + static_cast<int>(i % cols) * (cellW + 4),
            startY + static_cast<int>(i / cols) * stepY,
            cellW,
            cellH
        };
        std::string lbl = clipText(g_fileList[i], u.compactLabels ? 12 : 22);
        drawButton(btn, lbl.c_str(), rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);
    }
    if (g_fileList.empty()) {
        g_tft->drawString("No .mhb files found in LittleFS.", u.scrW / 2, u.scrH / 2);
    }
}

static void drawKeyboard() {
    const auto& u = UI();
    int padX = u.compactLabels ? 4 : 40;
    int padY = u.compactLabels ? 24 : 30;
    int mw = u.scrW - padX * 2;
    int mh = u.scrH - padY - 4;

    g_tft->fillRect(padX, padY, mw, mh, rgb565(15, 20, 25));
    g_tft->drawRect(padX, padY, mw, mh, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(u.compactLabels ? "SAVE .MHB NAME:" : "ENTER FILE NAME TO SAVE (without .mhb):",
                      u.scrW / 2, padY + (u.compactLabels ? 10 : 30));

    int boxX = padX + (u.compactLabels ? 8 : 60);
    int boxY = padY + (u.compactLabels ? 20 : 70);
    int boxW = mw - (u.compactLabels ? 64 : 120);
    int boxH = u.compactLabels ? 22 : 50;

    g_tft->fillRect(boxX, boxY, boxW, boxH, rgb565(37, 40, 48));
    g_tft->drawRect(boxX, boxY, boxW, boxH, rgb565(255, 202, 40));
    g_tft->setTextColor(0xFFFF, rgb565(37, 40, 48));
    std::string disp = g_kbdInput + "_";
    g_tft->drawString(disp.c_str(), boxX + boxW / 2, boxY + boxH / 2);

    TouchButton delBtn = u.compactLabels
                             ? TouchButton{boxX + boxW + 4, boxY, 44, boxH}
                             : TouchButton{630, 170, 80, 40};
    drawButton(delBtn, "DEL", rgb565(230, 81, 0));

    const char* rows[] = {"1234567890-", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_"};
    int keyStepX = u.compactLabels ? 27 : 45;
    int keyW     = u.compactLabels ? 25 : 40;
    int keyH     = u.compactLabels ? 24 : 40;
    int keyStepY = u.compactLabels ? 27 : 50;
    int yStart   = padY + (u.compactLabels ? 48 : 140);

    for (int r = 0; r < 4; ++r) {
        int len = static_cast<int>(std::strlen(rows[r]));
        int xStart = u.scrW / 2 - (len * keyStepX) / 2;
        for (int c = 0; c < len; ++c) {
            TouchButton btn = {xStart + c * keyStepX, yStart + r * keyStepY, keyW, keyH};
            char k[2] = {rows[r][c], '\0'};
            drawButton(btn, k, rgb565(55, 71, 79), rgb565(0, 172, 193), 0xFFFF);
        }
    }

    int botY = u.compactLabels ? (u.scrH - 30) : 380;
    int botH = u.compactLabels ? 24 : 45;
    int botW = u.compactLabels ? 86 : 120;
    int cx = u.scrW / 2;
    drawButton({cx - botW - (botW / 2) - 8, botY, botW, botH}, "CANCEL", rgb565(183, 28, 28));
    drawButton({cx - (botW / 2),             botY, botW, botH}, "SPACE",  rgb565(55, 71, 79));
    drawButton({cx + (botW / 2) + 8,         botY, botW, botH}, "SAVE",   rgb565(46, 125, 50));
}

static void drawTopTransportRow() {
    const auto& u = UI();
    const auto& lang = L();
    const bool compact = u.compactLabels;

    drawButton(u.btnStart, engine.isPlaying() ? lang.btnStop : lang.btnStart,
               engine.isPlaying() ? rgb565(183, 28, 28) : rgb565(27, 94, 32));
    drawButton(u.btnPause, engine.isPaused() ? lang.btnResume : lang.btnPause, rgb565(230, 81, 0));

    std::string cntLbl = compact
                             ? (g_useCountdown ? "C:1" : "C:0")
                             : (std::string(lang.chkCountdown) + ":" + (g_useCountdown ? lang.stateOn : lang.stateOff));
    drawButton(u.btnCount, cntLbl.c_str(),
               g_useCountdown ? rgb565(0, 105, 92) : rgb565(55, 65, 81));

    drawButton(u.btnTalaPrev, "<", rgb565(0, 77, 64));
    std::string talaName = clipText(BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name, compact ? 7 : 18);
    drawButton(u.talaBox, talaName.c_str(), rgb565(0, 77, 64), rgb565(0, 229, 255));
    drawButton(u.btnTalaNext, ">", rgb565(0, 77, 64));

    drawButton(u.btnSongPrev, "<", rgb565(74, 20, 140));
    std::string songName = clipText(BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title, compact ? 7 : 20);
    drawButton(u.songBox, songName.c_str(), rgb565(74, 20, 140), rgb565(255, 202, 40));
    drawButton(u.btnSongNext, ">", rgb565(74, 20, 140));

    drawButton(u.btnBpmDown, compact ? "-" : "-5", rgb565(43, 48, 60));
    char bpmStr[12];
    std::snprintf(bpmStr, sizeof(bpmStr), "%d", engine.getBpm());
    drawButton(u.bpmBox, bpmStr, rgb565(30, 34, 42));
    drawButton(u.btnBpmUp, compact ? "+" : "+5", rgb565(43, 48, 60));
}

static void drawSwaraPopup() {
    const auto& u = UI();
    const auto& lang = L();
    char buf[64];
    if (engine.isPlaying() && engine.getCountdownBeats() > 0) {
        std::snprintf(buf, sizeof(buf), "%s %d", u.compactLabels ? "CD:" : lang.countdownPrefix, engine.getCountdownBeats());
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
        if (u.compactLabels) {
            std::snprintf(buf, sizeof(buf), "%s (%s%d)", sw.c_str(), WESTERN_NOTES[midi % 12], westOct);
        } else {
            std::snprintf(buf, sizeof(buf), "%s %s (%s%d)", lang.swaraPopupPrefix, sw.c_str(), WESTERN_NOTES[midi % 12], westOct);
        }
    }
    drawButton(u.swaraBox, buf, rgb565(13, 37, 56), rgb565(0, 229, 255), rgb565(0, 229, 255));
}

static void drawDroneAndTunerRow() {
    const auto& u = UI();
    const auto& lang = L();
    const bool compact = u.compactLabels;

    drawSwaraPopup();

    std::string saStr = compact
                            ? (std::string("Sa:") + WESTERN_NOTES[engine.getSaSemitone()])
                            : (std::string(lang.saKeyPrefix) + lang.indianKeyLabels[engine.getSaSemitone()]);
    drawButton(u.btnSaCycle, saStr.c_str(), rgb565(37, 40, 48));

    bool dEn = engine.isDroneEnabled();
    const char* dLbl = compact ? (dEn ? "DRN:ON" : "DRN:OFF") : (dEn ? lang.btnDroneOn : lang.btnDroneOff);
    drawButton(u.btnDroneEn, dLbl,
               dEn ? rgb565(245, 127, 23) : rgb565(66, 66, 66),
               0xFFFF, dEn ? 0x0000 : 0xFFFF);

    const char* presetLbl = compact ? "Shr+Tan" : lang.dronePresetBoth;
    if (engine.getDronePreset() == 0) presetLbl = compact ? "Shruti" : lang.dronePresetShruti;
    else if (engine.getDronePreset() == 1) presetLbl = compact ? "Tanpura" : lang.dronePresetTanpura;
    drawButton(u.btnDronePre, presetLbl, rgb565(37, 40, 48));

    const char* surLblsFull[4]  = {lang.droneSurSaPa, lang.droneSurSaMa, lang.droneSurSaNi, lang.droneSurSaOnly};
    const char* surLblsShort[4] = {"S-P-S", "S-m-S", "S-N-S", "S-S"};
    int surMode = engine.getDroneSwaraMode() % 4;
    drawButton(u.btnDroneSur, compact ? surLblsShort[surMode] : surLblsFull[surMode], rgb565(37, 40, 48));
}

static TouchButton getTrackButtonRect(int idx) {
    const auto& u = UI();
    if (u.trkW <= 0 || u.trkH <= 0) return {0, 0, 0, 0};
    int col = idx % 6;
    int row = idx / 6;
    return {u.trkStartX + col * u.trkStepX, u.trkStartY + row * u.trkStepY, u.trkW, u.trkH};
}

static void drawTrackButtons() {
    const auto& u = UI();
    if (u.trkW <= 0) return;

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
        if (dispName == "Singing_Voice") dispName = (g_langIdx == 1) ? "गायन" : "Voice";

        std::string lbl;
        if (u.compactLabels) {
            lbl = clipText(dispName, 6);
        } else {
            lbl = dispName + " [" + (muted ? lang.stateOff : lang.stateOn) + "]";
        }
        drawButton(b, lbl.c_str(),
                   muted ? rgb565(55, 71, 79) : rgb565(46, 125, 50),
                   muted ? rgb565(85, 85, 85) : rgb565(129, 199, 132));
    }
}

static void drawUtilityRow() {
    const auto& u = UI();
    if (u.btnRndTala.w <= 0) return;

    const auto& lang = L();
    const bool compact = u.compactLabels;

    drawButton(u.btnRndTala, compact ? "RND TAL" : "RND TALA", rgb565(0, 121, 107), rgb565(255, 235, 59));
    drawButton(u.btnRndSong, compact ? "RND RAG" : "RND RAGA", rgb565(106, 27, 154), rgb565(255, 235, 59));

    int m = engine.getMidiInMode();
    const char* mStrFull  = (m == 0) ? "IN: DUAL (H+D)" : (m == 1) ? "IN: SPLIT (<52)" : (m == 2) ? "IN: HARM ONLY" : "IN: DRUM ONLY";
    const char* mStrShort = (m == 0) ? "MIDI:DUAL" : (m == 1) ? "MIDI:SPLT" : (m == 2) ? "MIDI:HARM" : "MIDI:DRUM";
    drawButton(u.btnMidiMode, compact ? mStrShort : mStrFull, rgb565(55, 71, 79));

    if (engine.hasSingingData()) {
        bool singOn = engine.isSingingEnabled();
        std::string singLbl;
        if (compact) {
            singLbl = (g_langIdx == 1) ? (singOn ? "गायन:1" : "गायन:0") : (singOn ? "VOICE:ON" : "VOICE:OFF");
        } else {
            singLbl = (g_langIdx == 1)
            ? (std::string("गायन (VOICE): ") + (singOn ? lang.stateOn : lang.stateOff))
            : (std::string("SINGING VOICE: ") + (singOn ? lang.stateOn : lang.stateOff));
        }
        drawButton(u.btnSingMode, singLbl.c_str(),
                   singOn ? rgb565(194, 24, 91) : rgb565(55, 71, 79),
                   singOn ? rgb565(255, 128, 171) : rgb565(100, 116, 139));
    } else {
        g_tft->fillRect(u.btnSingMode.x, u.btnSingMode.y, u.btnSingMode.w, u.btnSingMode.h, rgb565(22, 25, 30));
    }
}

static void drawHarmoniumDspRow() {
    const auto& u = UI();
    if (u.btnHarmMode.w <= 0) return;

    const auto& lang = L();
    const bool compact = u.compactLabels;
    bool presetOn = engine.isHarmoniumPresetMode();

    const char* harmLbl = compact
                              ? (presetOn ? "HARM:PRESET" : "HARM:LIVE")
                              : (presetOn ? lang.btnHarmPresetOn : lang.btnHarmPresetOff);
    drawButton(u.btnHarmMode, harmLbl,
               presetOn ? rgb565(239, 108, 0) : rgb565(0, 105, 92));

    drawButton(u.btnOctDown, compact ? "O-" : lang.btnOctDown, rgb565(43, 48, 60));
    char octBuf[32];
    std::snprintf(octBuf, sizeof(octBuf), "%s%d", compact ? "O:" : lang.lblOctavePrefix, engine.getOctaveShift());
    drawButton(u.octLbl, octBuf, rgb565(22, 25, 30));
    drawButton(u.btnOctUp, compact ? "O+" : lang.btnOctUp, rgb565(43, 48, 60));

    bool tfOn = engine.isTfEnabled();
    drawButton(u.btnTfEn, tfOn ? "FX:ON" : "FX:OFF",
               tfOn ? rgb565(40, 53, 147) : rgb565(51, 51, 51));

    drawButton(u.btnEditMode, compact ? "VOICE" : "HARM VOICING", rgb565(123, 31, 162), rgb565(255, 202, 40));
}

static void drawTalometerLEDs() {
    const auto& u = UI();
    if (u.taloH <= 0) return;

    g_tft->fillRect(0, u.taloY, u.scrW, u.taloH, rgb565(22, 25, 30));

    int curMatra = engine.getCurrentMatra() - 1;
    int talaIdx = engine.getSelectedTala();
    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[talaIdx];

    auto vibhags = parseVibhags(kt.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);

    int totalMatras = 0;
    for (int v : vibhags) totalMatras += v;
    if (totalMatras <= 0) return;

    int stepX = std::min(u.taloStepX, (u.scrW - 8) / totalMatras);
    int ledW  = std::max(4, stepX - 4);
    int ledH  = std::max(6, u.taloH - 4);
    int startX = (u.scrW - (totalMatras * stepX)) / 2;
    int ledY   = u.taloY + (u.taloH - ledH) / 2;

    int gMatra = 0;
    for (size_t v = 0; v < vibhags.size(); ++v) {
        bool isKhali = (tkLabels[v] == "0");

        for (int m = 0; m < vibhags[v]; ++m) {
            int lx = startX + gMatra * stepX + 2;
            bool active = (gMatra == curMatra && engine.isPlaying() && engine.getCountdownBeats() == 0);
            uint16_t color;

            if (m == 0) {
                if (isKhali) {
                    color = active ? rgb565(0, 255, 0) : rgb565(0, 75, 0);
                } else {
                    color = active ? rgb565(255, 50, 50) : rgb565(110, 0, 0);
                }
            } else {
                color = active ? rgb565(255, 202, 40) : rgb565(85, 65, 0);
            }

            g_tft->fillRoundRect(lx, ledY, ledW, ledH, 2, color);
            g_tft->drawRoundRect(lx, ledY, ledW, ledH, 2, active ? 0xFFFF : rgb565(120, 120, 120));
            gMatra++;
        }
    }
}

static void drawPianoKeyboard(int activeMidi = -1) {
    const auto& u = UI();
    if (!u.hasVisualPiano || u.pianoH <= 0) return;

    const auto& lang = L();
    int whiteCount = 0;
    for (int i = 0; i < PIANO_NUM_KEYS; ++i) {
        if (!isBlackKey(PIANO_START_MIDI + i)) whiteCount++;
    }
    float whiteW = static_cast<float>(u.scrW) / whiteCount;
    int blackH = static_cast<int>(u.pianoH * 0.60f);
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
        g_tft->fillRect(x, u.pianoY, w, u.pianoH, fillCol);
        g_tft->drawRect(x, u.pianoY, w, u.pianoH, rgb565(51, 51, 51));

        int rel = (midi - sa + 120) % 12;
        g_tft->setTextColor(0x0000, fillCol);
        g_tft->setTextDatum(BC_DATUM);
        g_tft->drawString(lang.swaraNames[rel], x + w / 2, u.pianoY + u.pianoH - 18);
        g_tft->drawString(WESTERN_NOTES[midi % 12], x + w / 2, u.pianoY + u.pianoH - 3);
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

        g_tft->fillRect(x, u.pianoY, w, blackH, fillCol);
        g_tft->drawRect(x, u.pianoY, w, blackH, 0x0000);

        int rel = (midi - sa + 120) % 12;
        g_tft->setTextColor(0xFFFF, fillCol);
        g_tft->setTextDatum(BC_DATUM);
        g_tft->drawString(lang.swaraNames[rel], x + w / 2, u.pianoY + blackH - 4);
    }
}

static void drawFooter() {
    const auto& u = UI();
    const auto& lang = L();

    g_tft->fillRect(0, u.footerY, u.scrW, u.footerH, rgb565(22, 25, 30));
    g_tft->setTextColor(rgb565(200, 200, 200), rgb565(22, 25, 30));
    g_tft->setTextDatum(ML_DATUM);

    const char* sTitle = BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title;
    const char* tName  = BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name;
    char leftBuf[160];

    if (u.compactLabels) {
        std::snprintf(leftBuf, sizeof(leftBuf), "M:%d/%d | %s",
                      engine.getCurrentMatra(), engine.getDrumLoopMatras(),
                      clipText(tName, 14).c_str());
    } else {
        std::snprintf(leftBuf, sizeof(leftBuf), "%s: %s | %s: %s (%s %d/%d)",
                      lang.footerSongLbl, sTitle, lang.footerTalaLbl, tName,
                      lang.footerMatraLbl, engine.getCurrentMatra(), engine.getDrumLoopMatras());
    }
    g_tft->drawString(leftBuf, 4, u.footerY + u.footerH / 2);

    g_tft->setTextDatum(MR_DATUM);
    g_tft->setTextColor(g_midiConnected ? rgb565(0, 229, 255) : rgb565(160, 160, 160),
                        rgb565(22, 25, 30));
    const char* midiLbl = u.compactLabels
                              ? (g_midiConnected ? "MIDI:OK" : "MIDI:WAIT")
                              : (g_midiConnected ? lang.midiConnected : lang.midiPcMode);
    g_tft->drawString(midiLbl, u.scrW - 4, u.footerY + u.footerH / 2);
}


// INSTRUMENT TEST SCREEN (OSCILLOSCOPE + FFT SPECTRUM ANALYZER + 24 TRIGGERS)


struct InstTestRects {
    TouchButton infoBanner;
    TouchButton btnRetrig;
    TouchButton btnAuto;
    TouchButton btnStop;
    TouchButton scopeBox;
    TouchButton specBox;
    TouchButton miniPrev, miniTrig, miniNext;
    int gridStartX, gridStartY, gridStepX, gridStepY, gridW, gridH;
};

static InstTestRects getInstTestRects() {
    int p = getActiveProfileIdx();
    if (p == 1) {
        // 320x240 SPI Screen Layout
        return {
            {  2,  26, 174, 18},
            {178,  26,  44, 18},
            {224,  26,  48, 18},
            {274,  26,  44, 18},
            {  2,  46, 156, 76},
            {162,  46, 156, 76},
            {  0,   0,   0,  0}, {0, 0, 0, 0}, {0, 0, 0, 0},
            2, 125, 53, 22, 51, 20
        };
    } else if (p == 2) {
        // 128x64 Monochrome Mini Layout
        return {
            {  0,   0,   0,  0},
            {  0,   0,   0,  0},
            {  0,   0,   0,  0},
            {  0,   0,   0,  0},
            {  1,  13,  62, 36},
            { 65,  13,  62, 36},
            {  1,  51,  18, 12}, {21, 51, 86, 12}, {109, 51, 18, 12},
            0, 0, 0, 0, 0, 0
        };
    }
    // 800x480 7" Layout
    return {
        {  6,  44, 454, 28},
        {466,  44, 106, 28},
        {578,  44, 112, 28},
        {696,  44,  98, 28},
        {  6,  76, 390, 166},
        {404,  76, 390, 166},
        {  0,   0,   0,   0}, {0, 0, 0, 0}, {0, 0, 0, 0},
        6, 248, 132, 50, 128, 45
    };
}

static TouchButton getTestInstButtonRect(int idx) {
    const auto r = getInstTestRects();
    if (r.gridW <= 0 || r.gridH <= 0) return {0, 0, 0, 0};
    int col = idx % 6;
    int row = idx / 6;
    return {r.gridStartX + col * r.gridStepX, r.gridStartY + row * r.gridStepY, r.gridW, r.gridH};
}

static std::string getTestInstrumentDisplayName(int idx, bool compact) {
    const auto& trks = BuiltInPresets::getTracks();
    if (idx < 0 || idx >= static_cast<int>(trks.size())) return "Inst";
    std::string raw = trks[idx].name;
    if (raw == "Harmonium_Bellows") return compact ? "Harm" : (g_langIdx == 1 ? "हारमोनियम" : "Harmonium");
    if (raw == "Singing_Voice")     return compact ? "Voice" : (g_langIdx == 1 ? "गायन (Voice)" : "Singing Voice");
    std::string tr = L().translateTrackName(raw);
    return compact ? clipText(tr, 6) : clipText(tr, 14);
}

static void drawSingleTestInstButton(int i) {
    if (i < 0 || i >= SynthEngine::NUM_TRACKS) return;
    TouchButton b = getTestInstButtonRect(i);
    if (b.w <= 0) return;

    const auto& u = UI();
    bool selected = (i == g_testSelectedInst);
    bool isSynth  = (i >= SynthEngine::TRACK_HARMONIUM);
    uint16_t bg = selected
                      ? rgb565(194, 24, 91)
                      : (isSynth ? rgb565(74, 20, 140) : rgb565(37, 45, 56));
    uint16_t brd = selected
                       ? rgb565(255, 235, 59)
                       : (isSynth ? rgb565(186, 104, 200) : rgb565(90, 105, 120));

    char lbl[40];
    if (u.compactLabels) {
        std::snprintf(lbl, sizeof(lbl), "%d:%s", i + 1, getTestInstrumentDisplayName(i, true).c_str());
    } else {
        std::snprintf(lbl, sizeof(lbl), "%02d. %s", i + 1, getTestInstrumentDisplayName(i, false).c_str());
    }
    drawButton(b, lbl, bg, brd, 0xFFFF);
}

// Fast 512-point in-place Radix-2 FFT (executes only when VIEW_INST_TEST is active)
static void computeTestFft512(const float* timeSamples, float* outMagBins, float& outPeakHz, float& outPeakDb) {
    static constexpr int N = 512;
    static bool s_tablesInit = false;
    static uint16_t s_bitRev[N];
    static float s_hann[N];
    static float s_cosLut[N / 2];
    static float s_sinLut[N / 2];

    if (!s_tablesInit) {
        for (int i = 0; i < N; ++i) {
            uint16_t rev = 0;
            for (int j = 0; j < 9; ++j) {
                if (i & (1 << j)) rev |= (1 << (8 - j));
            }
            s_bitRev[i] = rev;
            s_hann[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI_F * static_cast<float>(i) / static_cast<float>(N - 1)));
        }
        for (int k = 0; k < N / 2; ++k) {
            float ang = -2.0f * M_PI_F * static_cast<float>(k) / static_cast<float>(N);
            s_cosLut[k] = std::cos(ang);
            s_sinLut[k] = std::sin(ang);
        }
        s_tablesInit = true;
    }

    float re[N];
    float im[N];
    for (int i = 0; i < N; ++i) {
        re[i] = timeSamples[i] * s_hann[i];
        im[i] = 0.0f;
    }

    for (int i = 0; i < N; ++i) {
        int j = s_bitRev[i];
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int len = 2; len <= N; len <<= 1) {
        int half = len >> 1;
        int step = N / len;
        for (int i = 0; i < N; i += len) {
            for (int j = 0; j < half; ++j) {
                int twIdx = j * step;
                float wr = s_cosLut[twIdx];
                float wi = s_sinLut[twIdx];
                float tr = re[i + j + half] * wr - im[i + j + half] * wi;
                float ti = re[i + j + half] * wi + im[i + j + half] * wr;
                re[i + j + half] = re[i + j] - tr;
                im[i + j + half] = im[i + j] - ti;
                re[i + j] += tr;
                im[i + j] += ti;
            }
        }
    }

    int bestBin = 1;
    float maxMag = 1e-6f;
    outMagBins[0] = 0.0f;
    for (int k = 1; k < N / 2; ++k) {
        float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]) * (4.0f / static_cast<float>(N));
        outMagBins[k] = mag;
        if (mag > maxMag) {
            maxMag = mag;
            bestBin = k;
        }
    }

    // Parabolic interpolation around peak bin for accurate Hz readout
    float binOffset = 0.0f;
    if (bestBin > 1 && bestBin < (N / 2 - 1)) {
        float m0 = outMagBins[bestBin - 1];
        float m1 = outMagBins[bestBin];
        float m2 = outMagBins[bestBin + 1];
        float denom = (m0 - 2.0f * m1 + m2);
        if (std::fabs(denom) > 1e-6f) {
            binOffset = 0.5f * (m0 - m2) / denom;
        }
    }

    float binHz = engine.getSampleRate() / static_cast<float>(N);
    outPeakHz = (maxMag > 0.002f) ? ((static_cast<float>(bestBin) + binOffset) * binHz) : 0.0f;
    outPeakDb = 20.0f * std::log10(std::max(maxMag, 1e-4f));
}

static void drawInstTestHeaderAndButtons() {
    const auto& u = UI();
    const auto r = getInstTestRects();

    if (r.miniTrig.w > 0) {
        drawButton(r.miniPrev, "<", rgb565(55, 71, 79));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%s", g_testSelectedInst + 1,
                      getTestInstrumentDisplayName(g_testSelectedInst, true).c_str());
        drawButton(r.miniTrig, buf, rgb565(194, 24, 91), rgb565(255, 202, 40));
        drawButton(r.miniNext, ">", rgb565(55, 71, 79));
        return;
    }

    drawButton(r.btnRetrig, u.compactLabels ? "PLAY" : "▶ TRIGGER",
               rgb565(0, 121, 107), rgb565(0, 229, 255));
    drawButton(r.btnAuto,
               u.compactLabels ? (g_testAutoCycle ? "AUT:1" : "AUT:0")
                               : (g_testAutoCycle ? "AUTO CYCLE: ON" : "AUTO CYCLE: OFF"),
               g_testAutoCycle ? rgb565(230, 81, 0) : rgb565(55, 71, 79),
               rgb565(255, 202, 40));
    drawButton(r.btnStop, u.compactLabels ? "STOP" : "■ SILENCE",
               rgb565(183, 28, 28), 0xFFFF);

    for (int i = 0; i < SynthEngine::NUM_TRACKS; ++i) {
        drawSingleTestInstButton(i);
    }
}

static void updateInstTestVisualizers() {
    const auto& u = UI();
    const auto r = getInstTestRects();

    float samples[SynthEngine::SCOPE_RING_SIZE];
    engine.getScopeSamples(samples, SynthEngine::SCOPE_RING_SIZE);

    float liveMaxAbs = 0.0f;
    for (int i = 0; i < SynthEngine::SCOPE_RING_SIZE; ++i) {
        float a = std::fabs(samples[i]);
        if (a > liveMaxAbs) liveMaxAbs = a;
    }

    // Use live ring buffer when sustained sound is active; otherwise display
    // the captured 512-sample transient onset with smooth visual decay so
    // short 20ms hits (Ka, Ti, Re, Kartal Closed, Ra, Ki) are clearly visible.
    float maxAbs = liveMaxAbs;
    if (liveMaxAbs >= 0.025f) {
        std::memcpy(g_testCapturedWave, samples, sizeof(samples));
        g_testVisualDecay = 1.0f;
    } else if (g_testVisualDecay > 0.04f && g_testCaptureWriteIdx.load(std::memory_order_acquire) > 0) {
        maxAbs = 0.0f;
        for (int i = 0; i < SynthEngine::SCOPE_RING_SIZE; ++i) {
            samples[i] = g_testCapturedWave[i] * g_testVisualDecay;
            float a = std::fabs(samples[i]);
            if (a > maxAbs) maxAbs = a;
        }
        g_testVisualDecay *= 0.88f; // Smooth ~500ms visual persistence decay
    }

    float magBins[256];
    float peakHz = 0.0f;
    float peakDb = -80.0f;
    computeTestFft512(samples, magBins, peakHz, peakDb);

    g_tft->startWrite();

    // 1. Update Info Banner (Selected Instrument + Peak Frequency + Peak Amplitude)
    if (r.infoBanner.w > 0) {
        char infoBuf[96];
        std::string instName = getTestInstrumentDisplayName(g_testSelectedInst, u.compactLabels);
        if (u.compactLabels) {
            std::snprintf(infoBuf, sizeof(infoBuf), "#%02d %s | %.0fHz",
                          g_testSelectedInst + 1, instName.c_str(), peakHz);
        } else {
            std::snprintf(infoBuf, sizeof(infoBuf), "TEST #%02d: %s  |  Peak: %.0f Hz (%.1f dB)  |  Lvl: %d%%",
                          g_testSelectedInst + 1, instName.c_str(), peakHz, peakDb,
                          static_cast<int>(std::min(100.0f, maxAbs * 100.0f)));
        }
        drawButton(r.infoBanner, infoBuf, rgb565(15, 25, 38), rgb565(0, 229, 255), rgb565(0, 229, 255));
    }

    // 2. Render Oscilloscope (Left Box)
    const int sx = r.scopeBox.x;
    const int sy = r.scopeBox.y;
    const int sw = r.scopeBox.w;
    const int sh = r.scopeBox.h;
    const int headerPad = (u.scrH >= 400) ? 18 : (u.scrH >= 200 ? 12 : 0);

    g_tft->fillRect(sx + 1, sy + 1, sw - 2, sh - 2, rgb565(10, 14, 20));
    g_tft->drawRect(sx, sy, sw, sh, rgb565(0, 172, 193));

    int plotY0 = sy + headerPad + 2;
    int plotH  = sh - headerPad - 4;
    int midY   = plotY0 + plotH / 2;

    g_tft->drawFastHLine(sx + 2, midY, sw - 4, rgb565(30, 50, 65));
    g_tft->drawFastHLine(sx + 2, plotY0 + plotH / 4, sw - 4, rgb565(20, 32, 42));
    g_tft->drawFastHLine(sx + 2, plotY0 + (plotH * 3) / 4, sw - 4, rgb565(20, 32, 42));
    g_tft->drawFastVLine(sx + sw / 2, plotY0, plotH, rgb565(30, 50, 65));

    if (headerPad > 0) {
        g_tft->setTextColor(rgb565(0, 229, 255), rgb565(10, 14, 20));
        g_tft->setTextDatum(ML_DATUM);
        g_tft->drawString(u.compactLabels ? "OSCILLOSCOPE" : "OSCILLOSCOPE (TIME DOMAIN)", sx + 6, sy + headerPad / 2 + 1);
    }

    int trigOffset = 0;
    for (int i = 16; i < 240; ++i) {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
            trigOffset = i;
            break;
        }
    }

    int traceW = sw - 4;
    int availSamples = std::min(256, SynthEngine::SCOPE_RING_SIZE - trigOffset);
    float gainScale = (maxAbs > 0.005f && maxAbs < 0.35f) ? std::min(2.5f, 0.45f / maxAbs) : 1.0f;

    int prevX = sx + 2;
    int prevY = midY;
    for (int x = 0; x < traceW; ++x) {
        int sIdx = trigOffset + (x * (availSamples - 1)) / std::max(1, traceW - 1);
        float v = std::clamp(samples[sIdx] * gainScale, -1.0f, 1.0f);
        int py = midY - static_cast<int>(v * (plotH * 0.45f));
        py = std::clamp(py, plotY0, plotY0 + plotH - 1);
        int px = sx + 2 + x;
        if (x > 0) {
            g_tft->drawLine(prevX, prevY, px, py, rgb565(0, 255, 180));
            if (u.scrH >= 400 && py + 1 < plotY0 + plotH) {
                g_tft->drawLine(prevX, prevY + 1, px, py + 1, rgb565(0, 140, 100));
            }
        }
        prevX = px;
        prevY = py;
    }

    // 3. Render Spectrum Analyzer (Right Box)
    const int fx = r.specBox.x;
    const int fy = r.specBox.y;
    const int fw = r.specBox.w;
    const int fh = r.specBox.h;

    g_tft->fillRect(fx + 1, fy + 1, fw - 2, fh - 2, rgb565(10, 14, 20));
    g_tft->drawRect(fx, fy, fw, fh, rgb565(255, 179, 0));

    int fPlotY0   = fy + headerPad + 2;
    int labelPadB = (u.scrH >= 400) ? 14 : 2;
    int fPlotH    = fh - headerPad - labelPadB - 2;
    int fBotY     = fPlotY0 + fPlotH;

    g_tft->drawFastHLine(fx + 2, fPlotY0 + fPlotH / 3, fw - 4, rgb565(22, 32, 42));
    g_tft->drawFastHLine(fx + 2, fPlotY0 + (fPlotH * 2) / 3, fw - 4, rgb565(22, 32, 42));

    if (headerPad > 0) {
        g_tft->setTextColor(rgb565(255, 202, 40), rgb565(10, 14, 20));
        g_tft->setTextDatum(ML_DATUM);
        g_tft->drawString(u.compactLabels ? "SPECTRUM (FFT)" : "SPECTRUM ANALYZER (512-PT FFT)", fx + 6, fy + headerPad / 2 + 1);
    }

    static const int kBandStart[33] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 21, 24,
        28, 32, 37, 43, 50, 58, 67, 77, 88, 100, 114, 130, 148, 168, 190, 214, 245
    };
    const int numBands = (u.scrW < 160) ? 16 : 32;
    const int innerW   = fw - 8;
    const int stepW    = std::max(2, innerW / numBands);
    const int barW     = std::max(1, stepW - (u.scrW >= 400 ? 3 : 1));
    const int startBarX = fx + (fw - numBands * stepW) / 2;

    for (int b = 0; b < numBands; ++b) {
        int mapIdx = (numBands == 16) ? (b * 2) : b;
        int b0 = kBandStart[mapIdx];
        int b1 = (numBands == 16) ? kBandStart[mapIdx + 2] : kBandStart[mapIdx + 1];

        float bandPeak = 0.0f;
        for (int k = b0; k < b1 && k < 256; ++k) {
            if (magBins[k] > bandPeak) bandPeak = magBins[k];
        }

        float db = 20.0f * std::log10(std::max(bandPeak, 1e-4f));
        float norm = std::clamp((db + 52.0f) / 52.0f, 0.0f, 1.0f);

        if (norm >= g_specPeakHold[b]) {
            g_specPeakHold[b] = norm;
        } else {
            g_specPeakHold[b] = std::max(0.0f, g_specPeakHold[b] - 0.035f);
        }

        int barH  = static_cast<int>(norm * fPlotH);
        int peakH = static_cast<int>(g_specPeakHold[b] * fPlotH);
        int bx    = startBarX + b * stepW;

        if (barH > 0) {
            int midSplit = (fPlotH * 60) / 100;
            int topSplit = (fPlotH * 85) / 100;

            int hGreen = std::min(barH, midSplit);
            if (hGreen > 0) {
                g_tft->fillRect(bx, fBotY - hGreen, barW, hGreen, rgb565(0, 210, 140));
            }
            if (barH > midSplit) {
                int hYellow = std::min(barH, topSplit) - midSplit;
                g_tft->fillRect(bx, fBotY - midSplit - hYellow, barW, hYellow, rgb565(255, 202, 40));
            }
            if (barH > topSplit) {
                int hRed = barH - topSplit;
                g_tft->fillRect(bx, fBotY - barH, barW, hRed, rgb565(255, 64, 129));
            }
        }

        if (peakH > 2) {
            int py = std::max(fPlotY0, fBotY - peakH);
            g_tft->fillRect(bx, py, barW, 2, 0xFFFF);
        }
    }

    if (u.scrH >= 400) {
        g_tft->setTextColor(rgb565(160, 175, 190), rgb565(10, 14, 20));
        g_tft->setTextDatum(BC_DATUM);
        g_tft->drawString("100Hz", fx + 38,  fy + fh - 1);
        g_tft->drawString("500Hz", fx + 125, fy + fh - 1);
        g_tft->drawString("1.5k",  fx + 215, fy + fh - 1);
        g_tft->drawString("4kHz",  fx + 295, fy + fh - 1);
        g_tft->drawString("9kHz",  fx + 362, fy + fh - 1);
    }

    g_tft->endWrite();
}

static void drawInstTestScreen() {
    drawInstTestHeaderAndButtons();
    updateInstTestVisualizers();
    if (UI().scrH >= 400) {
        drawFooter();
    }
}


// PATTERN EDITORS  (Responsive across 800x480 and 320x240)


static void drawTalaEditor() {
    const auto& u = UI();
    auto& kt = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas())[engine.getSelectedTala()];
    drawEditorNavRow(kt.name, kt.divisions);

    auto vibhags = parseVibhags(kt.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);
    int globalMatra = 0;

    int padX  = u.compactLabels ? 4 : 16;
    int stepX = (u.scrW - padX * 2) / 8;
    int cellW = stepX - 2;
    int hdrH  = u.compactLabels ? 16 : 24;
    int bolH  = u.compactLabels ? 24 : 36;
    int karH  = u.compactLabels ? 24 : 36;
    int rowH  = hdrH + bolH + karH + (u.compactLabels ? 6 : 24);

    for (size_t v = 0; v < vibhags.size(); ++v) {
        for (int m = 0; m < vibhags[v]; ++m) {
            int screenRow = globalMatra / 8;
            int screenCol = globalMatra % 8;
            int bx = padX + screenCol * stepX;
            int by = u.edGridY + screenRow * rowH;

            char mBuf[32];
            if (m == 0) std::snprintf(mBuf, sizeof(mBuf), "[%s]%d", tkLabels[v].c_str(), globalMatra + 1);
            else        std::snprintf(mBuf, sizeof(mBuf), "M%d", globalMatra + 1);

            TouchButton headerBtn = {bx, by, cellW, hdrH};
            drawButton(headerBtn, mBuf, rgb565(38, 50, 56), rgb565(0, 172, 193), rgb565(0, 229, 255));

            TouchButton bolBtn = {bx, by + hdrH + 1, cellW, bolH};
            const char* curBol = (globalMatra < static_cast<int>(kt.bolCells.size())) ? kt.bolCells[globalMatra] : "-";
            std::string bolStr = clipText(curBol, u.compactLabels ? 5 : 12);
            drawButton(bolBtn, bolStr.c_str(), rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);

            TouchButton kartalBtn = {bx, by + hdrH + bolH + 2, cellW, karH};
            const char* curKar = (globalMatra < static_cast<int>(kt.kartalCells.size())) ? kt.kartalCells[globalMatra] : "-";
            std::string karStr = clipText(curKar, u.compactLabels ? 5 : 12);
            drawButton(kartalBtn, karStr.c_str(), rgb565(26, 35, 46), rgb565(60, 60, 60), rgb565(200, 200, 200));

            globalMatra++;
        }
    }
}

static void drawHarmEditor() {
    const auto& u = UI();
    auto& hs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs())[engine.getSelectedSong()];
    drawEditorNavRow(hs.title, hs.divisions);

    if (hs.forms.empty()) return;
    auto& form = hs.forms[0];
    auto vibhags = parseVibhags(hs.divisions);
    auto tkLabels = getTaliKhaliLabels(vibhags);

    int totalMatras = 0;
    for (int v : vibhags) totalMatras += v;
    if (totalMatras <= 0) totalMatras = 8;

    int padX  = u.compactLabels ? 4 : 16;
    int maxCW = u.compactLabels ? 36 : 64;
    int cellW = std::min(maxCW, (u.scrW - padX * 2) / totalMatras);
    int hdrH  = u.compactLabels ? 18 : 24;
    int rowH  = u.compactLabels ? 28 : 42;
    int btnH  = rowH - (u.compactLabels ? 3 : 4);

    int globalMatra = 0;
    for (size_t v = 0; v < vibhags.size(); ++v) {
        for (int m = 0; m < vibhags[v]; ++m) {
            char mBuf[32];
            if (m == 0) std::snprintf(mBuf, sizeof(mBuf), "%s%d", tkLabels[v].c_str(), globalMatra + 1);
            else        std::snprintf(mBuf, sizeof(mBuf), "%d", globalMatra + 1);

            TouchButton hBtn = {padX + globalMatra * cellW, u.edGridY, cellW - 1, hdrH};
            drawButton(hBtn, mBuf, (m == 0) ? rgb565(38, 50, 56) : rgb565(30, 34, 42),
                       (m == 0) ? rgb565(0, 172, 193) : rgb565(85, 85, 85),
                       (m == 0) ? rgb565(0, 229, 255) : rgb565(170, 170, 170));
            globalMatra++;
        }
    }

    int rowsY = u.edGridY + hdrH + (u.compactLabels ? 3 : 6);
    for (int r = 0; r < form.numRows; ++r) {
        for (int c = 0; c < totalMatras; ++c) {
            int cellIdx = r * totalMatras + c;
            TouchButton sBtn = {padX + c * cellW, rowsY + r * rowH, cellW - 1, btnH};
            const char* curSwara = (cellIdx < static_cast<int>(form.swaraCells.size())) ? form.swaraCells[cellIdx] : "-";
            drawButton(sBtn, curSwara, rgb565(26, 36, 46), rgb565(0, 172, 193), 0xFFFF);
        }
    }
}

static void drawModalSelector() {
    const auto& u = UI();
    int padX = u.compactLabels ? 4 : 40;
    int padY = u.compactLabels ? 24 : 30;
    int mw   = u.scrW - padX * 2;
    int mh   = u.scrH - padY - 4;

    g_tft->fillRect(padX, padY, mw, mh, rgb565(15, 20, 25));
    g_tft->drawRect(padX, padY, mw, mh, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(u.compactLabels ? "SELECT VALUE (Tap outside to exit)"
                                      : "SELECT VALUE (Tap to apply, tap outside to cancel)",
                      u.scrW / 2, padY + (u.compactLabels ? 10 : 20));

    const char** list = nullptr;
    int count = 0;
    if (g_modalState == MODAL_BOL) { list = VALID_BOLS; count = NUM_VALID_BOLS; }
    else if (g_modalState == MODAL_KARTAL) { list = VALID_KARTALS; count = NUM_VALID_KARTALS; }
    else if (g_modalState == MODAL_SWARA) { list = VALID_SWARAS; count = NUM_VALID_SWARAS; }

    int cols   = 6;
    int gapX   = u.compactLabels ? 3 : 10;
    int gapY   = u.compactLabels ? 4 : 10;
    int cellW  = (mw - (cols + 1) * gapX) / cols;
    int cellH  = u.compactLabels ? 30 : 45;
    int startX = u.scrW / 2 - (cols * (cellW + gapX)) / 2;
    int startY = padY + (u.compactLabels ? 22 : 50);

    for (int i = 0; i < count; ++i) {
        int c = i % cols;
        int r = i / cols;
        TouchButton btn = {startX + c * (cellW + gapX), startY + r * (cellH + gapY), cellW, cellH};
        std::string lbl = clipText(list[i], u.compactLabels ? 7 : 16);
        drawButton(btn, lbl.c_str(), rgb565(37, 40, 48), rgb565(85, 85, 85), 0xFFFF);
    }
}

static void drawGpioModal() {
    const auto& u = UI();
    int padX = u.compactLabels ? 4 : 60;
    int padY = u.compactLabels ? 24 : 44;
    int mw   = u.scrW - padX * 2;
    int mh   = u.scrH - padY - 4;

    g_tft->fillRect(padX, padY, mw, mh, rgb565(15, 20, 25));
    g_tft->drawRect(padX, padY, mw, mh, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(u.compactLabels ? "I2S, I2C & GPIO PIN SETUP" : "BOARD GPIO, I2S DAC & I2C PIN SETUP",
                      u.scrW / 2, padY + (u.compactLabels ? 9 : 22));

    const char* labelsFull[7] = {
        "I2S Audio BCLK (Bit Clock):",
        "I2S Audio LRCK / WS (Word Sel):",
        "I2S Audio DOUT (Data Out):",
        "I2C Bus SDA (Data Pin):",
        "I2C Bus SCL (Clock Pin):",
        "Bellows POS (Position ADC Pin):",
        "Bellows DEP (Depth/Press Pin):"
    };
    const char* labelsShort[7] = {
        "I2S BCLK:",
        "I2S LRCK:",
        "I2S DOUT:",
        "I2C SDA:",
        "I2C SCL:",
        "Bel POS:",
        "Bel DEP:"
    };
    int vals[7] = {
        g_pins.i2sBclk, g_pins.i2sLrck, g_pins.i2sDout,
        g_pins.i2cSda,  g_pins.i2cScl,
        g_pins.belPosPin, g_pins.belDepPin
    };

    int startY = padY + (u.compactLabels ? 18 : 46);
    int stepY  = u.compactLabels ? 23 : 40;
    int rowH   = u.compactLabels ? 19 : 30;
    int lblX   = padX + (u.compactLabels ? 8 : 30);
    int minusX = u.compactLabels ? 140 : 430;
    int minusW = u.compactLabels ? 32  : 56;
    int valX   = u.compactLabels ? 176 : 494;
    int valW   = u.compactLabels ? 96  : 110;
    int plusX  = u.compactLabels ? 276 : 612;

    for (int i = 0; i < 7; ++i) {
        int ry = startY + i * stepY;
        g_tft->setTextColor(0xFFFF, rgb565(15, 20, 25));
        g_tft->setTextDatum(ML_DATUM);
        g_tft->drawString(u.compactLabels ? labelsShort[i] : labelsFull[i], lblX, ry + rowH / 2);

        TouchButton btnMinus = {minusX, ry, minusW, rowH};
        TouchButton valBox   = {valX,   ry, valW,   rowH};
        TouchButton btnPlus  = {plusX,  ry, minusW, rowH};

        char vBuf[24];
        if (vals[i] < 0) std::snprintf(vBuf, sizeof(vBuf), (i == 6) ? "AUTO SPD" : "OFF (-1)");
        else             std::snprintf(vBuf, sizeof(vBuf), "GPIO %d", vals[i]);

        drawButton(btnMinus, "-", rgb565(55, 71, 79));
        drawButton(valBox, vBuf, rgb565(37, 40, 48), rgb565(0, 229, 255));
        drawButton(btnPlus, "+", rgb565(55, 71, 79));
    }

    int botY = u.compactLabels ? (u.scrH - 32) : 385;
    int botH = u.compactLabels ? 24 : 40;
    int botW = u.compactLabels ? 110 : 160;
    drawButton({u.scrW / 2 - botW - 10, botY, botW, botH}, "SAVE & APPLY", rgb565(46, 125, 50));
    drawButton({u.scrW / 2 + 10,        botY, botW, botH}, "CLOSE",        rgb565(183, 28, 28));
}

static void drawHarmConfigModal() {
    const auto& u = UI();
    const auto& p = engine.getHarmoniumParams();
    int padX = u.compactLabels ? 4 : 36;
    int padY = u.compactLabels ? 24 : 42;
    int mw   = u.scrW - padX * 2;
    int mh   = u.scrH - padY - 4;

    g_tft->fillRect(padX, padY, mw, mh, rgb565(15, 20, 25));
    g_tft->drawRect(padX, padY, mw, mh, rgb565(0, 229, 255));
    g_tft->setTextColor(rgb565(255, 202, 40), rgb565(15, 20, 25));
    g_tft->setTextDatum(MC_DATUM);
    g_tft->drawString(u.compactLabels ? "HARMONIUM STOPS & VOICING"
                                      : "HARMONIUM REED STOPS, COUPLER & BELLOWS VOICING",
                      u.scrW / 2, padY + (u.compactLabels ? 9 : 18));

    const char* labels[9] = {
        "16' Bass Stop (Kharaj)",
        "8' Principal (Nar)",
        "8' Celeste (Mada)",
        "4' Octave Coupler",
        "Celeste Detune (Cents)",
        "Jali Lid Brightness",
        "Bellows Attack Speed",
        "Key Release Damping",
        "Bellows Tremolo Depth"
    };
    const char* shortLabels[9] = {
        "16' Bass", "8' Princ", "8' Celest", "4' Couplr",
        "Detune c", "Lid Brt",  "Atk Rate",  "Rel Rate", "Pump Mod"
    };
    float vals[9] = {
        p.bassStop, p.principalStop, p.celesteStop, p.couplerStop,
        p.celesteCents, p.brightness, p.attackRate, p.releaseRate, p.pumpModDepth
    };

    int colW   = (mw - 12) / 2;
    int startY = padY + (u.compactLabels ? 20 : 38);
    int stepY  = u.compactLabels ? 26 : 52;
    int rowH   = u.compactLabels ? 22 : 38;

    for (int i = 0; i < 9; ++i) {
        int col = (i >= 5) ? 1 : 0;
        int row = (i >= 5) ? (i - 5) : i;
        int cx  = padX + 6 + col * colW;
        int ry  = startY + row * stepY;

        g_tft->setTextColor(0xFFFF, rgb565(15, 20, 25));
        g_tft->setTextDatum(ML_DATUM);
        g_tft->drawString(u.compactLabels ? shortLabels[i] : labels[i], cx, ry + rowH / 2);

        int btnW   = u.compactLabels ? 20 : 36;
        int valW   = u.compactLabels ? 36 : 64;
        int rightX = cx + colW - 6;
        TouchButton btnPlus  = {rightX - btnW,                ry, btnW, rowH};
        TouchButton valBox   = {rightX - btnW - valW - 2,     ry, valW, rowH};
        TouchButton btnMinus = {rightX - btnW * 2 - valW - 4, ry, btnW, rowH};

        char vBuf[24];
        if (i == 4)      std::snprintf(vBuf, sizeof(vBuf), "+%.0fc", vals[i]);
        else if (i >= 6 && i <= 7) std::snprintf(vBuf, sizeof(vBuf), "%.0f", vals[i]);
        else             std::snprintf(vBuf, sizeof(vBuf), "%d%%", static_cast<int>(std::lround(vals[i] * 100.0f)));

        drawButton(btnMinus, "-", rgb565(55, 71, 79));
        drawButton(valBox, vBuf, rgb565(37, 40, 48), rgb565(0, 229, 255));
        drawButton(btnPlus, "+", rgb565(55, 71, 79));
    }

    if (!u.compactLabels) {
        std::string fPreview = "Formula: " + clipText(engine.getActiveHarmoniumFormula(), 78) + "...";
        g_tft->setTextColor(rgb565(0, 229, 255), rgb565(15, 20, 25));
        g_tft->setTextDatum(MC_DATUM);
        g_tft->drawString(fPreview.c_str(), u.scrW / 2, padY + mh - 66);
    }

    int botY = u.compactLabels ? (u.scrH - 28) : (padY + mh - 46);
    int botH = u.compactLabels ? 22 : 38;
    int botW = u.compactLabels ? 96 : 180;
    int cx   = u.scrW / 2;
    drawButton({cx - botW - botW / 2 - 8, botY, botW, botH}, u.compactLabels ? "SAVE"  : "SAVE SETTINGS", rgb565(46, 125, 50));
    drawButton({cx - botW / 2,            botY, botW, botH}, u.compactLabels ? "RESET" : "RESET DEFAULT", rgb565(230, 81, 0));
    drawButton({cx + botW / 2 + 8,        botY, botW, botH}, u.compactLabels ? "CLOSE" : "CLOSE",         rgb565(183, 28, 28));
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
    if (g_modalState == MODAL_GPIO_CFG) {
        drawGpioModal();
        return;
    }
    if (g_modalState == MODAL_HARM_CFG) {
        drawHarmConfigModal();
        return;
    }

    if (g_modalState != MODAL_NONE) {
        if (g_currentView == VIEW_TALA_ED) drawTalaEditor();
        else if (g_currentView == VIEW_HARM_ED) drawHarmEditor();
        else if (g_currentView == VIEW_INST_TEST) drawInstTestScreen();
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
    } else if (g_currentView == VIEW_INST_TEST) {
        drawInstTestScreen();
    }
}

static void switchAppView(AppView nextView) {
    if (g_currentView == nextView) return;
    if (g_currentView == VIEW_INST_TEST && nextView != VIEW_INST_TEST) {
        engine.setTestScopeEnabled(false);
        engine.silenceAllVoices();
        g_testAutoCycle = false;
        g_testVisualDecay = 0.0f;
        g_testCaptureWriteIdx.store(SynthEngine::SCOPE_RING_SIZE, std::memory_order_release);
    } else if (nextView == VIEW_INST_TEST) {
        std::memset(g_specPeakHold, 0, sizeof(g_specPeakHold));
        std::memset(g_testCapturedWave, 0, sizeof(g_testCapturedWave));
        g_testVisualDecay = 0.0f;
        engine.setTestScopeEnabled(true);
        g_testAutoTicks = 0;
    }
    g_currentView = nextView;
    redrawEntireScreen();
}


// MAIN TOUCH & EVENT DISPATCH LOOP


static bool wasTouching = false;

static void esp32LoopTick() {
    pollBellowsGpioPins();

    if (g_harmPreviewTicks > 0) {
        if (--g_harmPreviewTicks == 0 && g_harmPreviewMidi >= 0) {
            engine.noteOff(g_harmPreviewMidi);
            g_harmPreviewMidi = -1;
        }
    }

    const auto& u = UI();
    uint16_t tx = 0, ty = 0;
    bool touching = g_tft->getTouch(&tx, &ty);

    if (touching && !wasTouching) {
        if (g_modalState == MODAL_GPIO_CFG) {
            int padY   = u.compactLabels ? 24 : 44;
            int startY = padY + (u.compactLabels ? 18 : 46);
            int stepY  = u.compactLabels ? 23 : 40;
            int rowH   = u.compactLabels ? 19 : 30;
            int minusX = u.compactLabels ? 140 : 430;
            int minusW = u.compactLabels ? 32  : 56;
            int plusX  = u.compactLabels ? 276 : 612;

            int* pinPtrs[7] = {
                &g_pins.i2sBclk, &g_pins.i2sLrck, &g_pins.i2sDout,
                &g_pins.i2cSda,  &g_pins.i2cScl,
                &g_pins.belPosPin, &g_pins.belDepPin
            };
            for (int i = 0; i < 7; ++i) {
                int ry = startY + i * stepY;
                int minPin = (i >= 3) ? -1 : 0;
                if (hitBox({minusX, ry, minusW, rowH}, tx, ty)) {
                    *pinPtrs[i] = std::max(minPin, *pinPtrs[i] - 1);
                } else if (hitBox({plusX, ry, minusW, rowH}, tx, ty)) {
                    *pinPtrs[i] = std::min(48, *pinPtrs[i] + 1);
                }
            }

            int botY = u.compactLabels ? (u.scrH - 32) : 385;
            int botH = u.compactLabels ? 24 : 40;
            int botW = u.compactLabels ? 110 : 160;
            if (hitBox({u.scrW / 2 - botW - 10, botY, botW, botH}, tx, ty)) {
                saveGpioConfig();
                g_modalState = MODAL_NONE;
            } else if (hitBox({u.scrW / 2 + 10, botY, botW, botH}, tx, ty)) {
                g_modalState = MODAL_NONE;
            }
            redrawEntireScreen();

        } else if (g_modalState == MODAL_HARM_CFG) {
            HarmoniumParams p = engine.getHarmoniumParams();
            int padX   = u.compactLabels ? 4 : 36;
            int padY   = u.compactLabels ? 24 : 42;
            int mw     = u.scrW - padX * 2;
            int mh     = u.scrH - padY - 4;
            int colW   = (mw - 12) / 2;
            int startY = padY + (u.compactLabels ? 20 : 38);
            int stepY  = u.compactLabels ? 26 : 52;
            int rowH   = u.compactLabels ? 22 : 38;
            int btnW   = u.compactLabels ? 20 : 36;
            int valW   = u.compactLabels ? 36 : 64;

            float* ptrs[9] = {
                &p.bassStop, &p.principalStop, &p.celesteStop, &p.couplerStop,
                &p.celesteCents, &p.brightness, &p.attackRate, &p.releaseRate, &p.pumpModDepth
            };
            const float steps[9] = {0.05f, 0.05f, 0.05f, 0.05f, 1.0f, 0.05f, 1.0f, 1.0f, 0.02f};

            bool changed = false;
            for (int i = 0; i < 9; ++i) {
                int col = (i >= 5) ? 1 : 0;
                int row = (i >= 5) ? (i - 5) : i;
                int cx  = padX + 6 + col * colW;
                int ry  = startY + row * stepY;
                int rightX = cx + colW - 6;

                TouchButton btnPlus  = {rightX - btnW,                ry, btnW, rowH};
                TouchButton btnMinus = {rightX - btnW * 2 - valW - 4, ry, btnW, rowH};
                if (hitBox(btnMinus, tx, ty)) { *ptrs[i] -= steps[i]; changed = true; }
                else if (hitBox(btnPlus, tx, ty)) { *ptrs[i] += steps[i]; changed = true; }
            }
            if (changed) {
                engine.applyHarmoniumParams(p);
                if (!engine.isPlaying()) {
                    if (g_harmPreviewMidi >= 0) engine.noteOff(g_harmPreviewMidi);
                    g_harmPreviewMidi = 60 + engine.getSaSemitone();
                    engine.noteOn(g_harmPreviewMidi, 100);
                    g_harmPreviewTicks = 24;
                }
            }

            int botY = u.compactLabels ? (u.scrH - 28) : (padY + mh - 46);
            int botH = u.compactLabels ? 22 : 38;
            int botW = u.compactLabels ? 96 : 180;
            int cx   = u.scrW / 2;
            if (hitBox({cx - botW - botW / 2 - 8, botY, botW, botH}, tx, ty)) {
                if (g_harmPreviewMidi >= 0) { engine.noteOff(g_harmPreviewMidi); g_harmPreviewMidi = -1; g_harmPreviewTicks = 0; }
                saveHarmoniumConfig();
                g_modalState = MODAL_NONE;
            } else if (hitBox({cx - botW / 2, botY, botW, botH}, tx, ty)) {
                if (g_harmPreviewMidi >= 0) { engine.noteOff(g_harmPreviewMidi); g_harmPreviewMidi = -1; g_harmPreviewTicks = 0; }
                eraseHarmoniumConfigToDefault();
            } else if (hitBox({cx + botW / 2 + 8, botY, botW, botH}, tx, ty)) {
                if (g_harmPreviewMidi >= 0) { engine.noteOff(g_harmPreviewMidi); g_harmPreviewMidi = -1; g_harmPreviewTicks = 0; }
                g_modalState = MODAL_NONE;
            }
            redrawEntireScreen();

        } else if (hitBox(u.btnBelPos, tx, ty)) {
            engine.setBellowsPosManual(!engine.isBellowsPosManual());
            drawTabs();
        } else if (hitBox(u.btnBelDep, tx, ty)) {
            engine.setBellowsDepManual(!engine.isBellowsDepManual());
            drawTabs();
        } else if (hitBox(u.btnGpio, tx, ty)) {
            g_modalState = MODAL_GPIO_CFG;
            redrawEntireScreen();
        } else if (hitBox(u.btnAudio, tx, ty)) {
            g_audioOutMode = (g_audioOutMode + 1) % 3;
#ifdef ESP32
            UsbAudioPartner::s_enableUsbAudio = (g_audioOutMode == 0);
#endif
            redrawEntireScreen();

        } else if (g_modalState == MODAL_FILE_LOAD) {
            int padX = u.compactLabels ? 6 : 40;
            int padY = u.compactLabels ? 24 : 30;
            int mw = u.scrW - padX * 2;
            int cols = 3;
            int cellW = (mw - 24) / cols;
            int cellH = u.compactLabels ? 28 : 45;
            int stepY = u.compactLabels ? 34 : 60;
            int startY = padY + (u.compactLabels ? 26 : 70);

            for (size_t i = 0; i < g_fileList.size() && i < 12; ++i) {
                TouchButton btn = {
                    padX + 8 + static_cast<int>(i % cols) * (cellW + 4),
                    startY + static_cast<int>(i / cols) * stepY,
                    cellW,
                    cellH
                };
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
            int padX = u.compactLabels ? 4 : 40;
            int padY = u.compactLabels ? 24 : 30;
            int mw   = u.scrW - padX * 2;
            int boxX = padX + (u.compactLabels ? 8 : 60);
            int boxY = padY + (u.compactLabels ? 20 : 70);
            int boxW = mw - (u.compactLabels ? 64 : 120);
            int boxH = u.compactLabels ? 22 : 50;

            int keyStepX = u.compactLabels ? 27 : 45;
            int keyW     = u.compactLabels ? 25 : 40;
            int keyH     = u.compactLabels ? 24 : 40;
            int keyStepY = u.compactLabels ? 27 : 50;
            int yStart   = padY + (u.compactLabels ? 48 : 140);

            for (int r = 0; r < 4; ++r) {
                int len = static_cast<int>(std::strlen(rows[r]));
                int xStart = u.scrW / 2 - (len * keyStepX) / 2;
                for (int c = 0; c < len; ++c) {
                    if (hitBox({xStart + c * keyStepX, yStart + r * keyStepY, keyW, keyH}, tx, ty)) {
                        if (g_kbdInput.length() < 24) g_kbdInput += rows[r][c];
                        handled = true;
                        break;
                    }
                }
                if (handled) break;
            }
            if (!handled) {
                TouchButton delBtn = u.compactLabels
                                         ? TouchButton{boxX + boxW + 4, boxY, 44, boxH}
                                         : TouchButton{630, 170, 80, 40};
                int botY = u.compactLabels ? (u.scrH - 30) : 380;
                int botH = u.compactLabels ? 24 : 45;
                int botW = u.compactLabels ? 86 : 120;
                int cx = u.scrW / 2;

                if (hitBox(delBtn, tx, ty)) {
                    if (!g_kbdInput.empty()) g_kbdInput.pop_back();
                } else if (hitBox({cx - (botW / 2), botY, botW, botH}, tx, ty)) {
                    if (g_kbdInput.length() < 24) g_kbdInput += " ";
                } else if (hitBox({cx - botW - (botW / 2) - 8, botY, botW, botH}, tx, ty)) {
                    g_modalState = MODAL_NONE;
                } else if (hitBox({cx + (botW / 2) + 8, botY, botW, botH}, tx, ty)) {
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

            int padX   = u.compactLabels ? 4 : 40;
            int padY   = u.compactLabels ? 24 : 30;
            int mw     = u.scrW - padX * 2;
            int cols   = 6;
            int gapX   = u.compactLabels ? 3 : 10;
            int gapY   = u.compactLabels ? 4 : 10;
            int cellW  = (mw - (cols + 1) * gapX) / cols;
            int cellH  = u.compactLabels ? 30 : 45;
            int startX = u.scrW / 2 - (cols * (cellW + gapX)) / 2;
            int startY = padY + (u.compactLabels ? 22 : 50);
            bool selectionMade = false;

            for (int i = 0; i < count; ++i) {
                TouchButton btn = {startX + (i % cols) * (cellW + gapX), startY + (i / cols) * (cellH + gapY), cellW, cellH};
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

        } else if (hitBox(u.btnLang, tx, ty)) {
            g_langIdx = (g_langIdx + 1) % static_cast<int>(I18n::getLanguages().size());
            redrawEntireScreen();
        } else if (hitBox(u.tabLive, tx, ty)) {
            switchAppView(VIEW_LIVE);
        } else if (hitBox(u.tabTala, tx, ty)) {
            switchAppView(VIEW_TALA_ED);
        } else if (hitBox(u.tabHarm, tx, ty)) {
            switchAppView(VIEW_HARM_ED);
        } else if (hitBox(u.tabTest, tx, ty)) {
            switchAppView(VIEW_INST_TEST);
        } else if (g_currentView == VIEW_INST_TEST) {
            const auto r = getInstTestRects();
            if (r.miniTrig.w > 0) {
                if (hitBox(r.miniPrev, tx, ty)) {
                    g_testSelectedInst = (g_testSelectedInst + SynthEngine::NUM_TRACKS - 1) % SynthEngine::NUM_TRACKS;
                    triggerAndCaptureTestInstrument(g_testSelectedInst);
                    drawInstTestHeaderAndButtons();
                } else if (hitBox(r.miniNext, tx, ty)) {
                    g_testSelectedInst = (g_testSelectedInst + 1) % SynthEngine::NUM_TRACKS;
                    triggerAndCaptureTestInstrument(g_testSelectedInst);
                    drawInstTestHeaderAndButtons();
                } else if (hitBox(r.miniTrig, tx, ty)) {
                    triggerAndCaptureTestInstrument(g_testSelectedInst);
                }
            } else if (hitBox(r.btnRetrig, tx, ty) || hitBox(r.infoBanner, tx, ty)) {
                triggerAndCaptureTestInstrument(g_testSelectedInst);
            } else if (hitBox(r.btnAuto, tx, ty)) {
                g_testAutoCycle = !g_testAutoCycle;
                g_testAutoTicks = 0;
                if (g_testAutoCycle) {
                    triggerAndCaptureTestInstrument(g_testSelectedInst);
                }
                drawButton(r.btnAuto,
                           u.compactLabels ? (g_testAutoCycle ? "AUT:1" : "AUT:0")
                                           : (g_testAutoCycle ? "AUTO CYCLE: ON" : "AUTO CYCLE: OFF"),
                           g_testAutoCycle ? rgb565(230, 81, 0) : rgb565(55, 71, 79),
                           rgb565(255, 202, 40));
            } else if (hitBox(r.btnStop, tx, ty)) {
                g_testAutoCycle = false;
                g_testVisualDecay = 0.0f;
                std::memset(g_testCapturedWave, 0, sizeof(g_testCapturedWave));
                engine.silenceAllVoices();
                drawInstTestHeaderAndButtons();
            } else {
                for (int i = 0; i < SynthEngine::NUM_TRACKS; ++i) {
                    if (hitBox(getTestInstButtonRect(i), tx, ty)) {
                        int prevInst = g_testSelectedInst;
                        g_testSelectedInst = i;
                        triggerAndCaptureTestInstrument(i);
                        if (prevInst != i) drawSingleTestInstButton(prevInst);
                        drawSingleTestInstButton(i);
                        break;
                    }
                }
            }
        } else if (g_currentView == VIEW_LIVE) {
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
                if (hitBox(u.btnStart, tx, ty)) {
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    else engine.stopPlayback();
                    drawTopTransportRow();
                } else if (hitBox(u.btnPause, tx, ty)) {
                    engine.togglePause();
                    drawTopTransportRow();
                } else if (hitBox(u.btnCount, tx, ty)) {
                    g_useCountdown = !g_useCountdown;
                    drawTopTransportRow();
                } else if (hitBox(u.btnTalaPrev, tx, ty)) {
                    engine.selectTala(engine.getSelectedTala() - 1);
                    drawTopTransportRow();
                    drawTalometerLEDs();
                    drawFooter();
                } else if (hitBox(u.btnTalaNext, tx, ty)) {
                    engine.selectTala(engine.getSelectedTala() + 1);
                    drawTopTransportRow();
                    drawTalometerLEDs();
                    drawFooter();
                } else if (hitBox(u.btnSongPrev, tx, ty)) {
                    engine.selectHarmoniumSong(engine.getSelectedSong() - 1);
                    drawTopTransportRow();
                    drawUtilityRow();
                    drawTrackButtons();
                    drawFooter();
                } else if (hitBox(u.btnSongNext, tx, ty)) {
                    engine.selectHarmoniumSong(engine.getSelectedSong() + 1);
                    drawTopTransportRow();
                    drawUtilityRow();
                    drawTrackButtons();
                    drawFooter();
                } else if (hitBox(u.btnBpmDown, tx, ty)) {
                    engine.setBpm(engine.getBpm() - 5);
                    drawTopTransportRow();
                } else if (hitBox(u.btnBpmUp, tx, ty)) {
                    engine.setBpm(engine.getBpm() + 5);
                    drawTopTransportRow();
                } else if (hitBox(u.btnSaCycle, tx, ty)) {
                    engine.setSaSemitone(engine.getSaSemitone() + 1);
                    drawDroneAndTunerRow();
                    drawPianoKeyboard(g_touchHeldMidi);
                } else if (hitBox(u.btnDroneEn, tx, ty)) {
                    engine.setDroneEnabled(!engine.isDroneEnabled());
                    drawDroneAndTunerRow();
                } else if (hitBox(u.btnDronePre, tx, ty)) {
                    engine.setDronePreset(engine.getDronePreset() + 1);
                    drawDroneAndTunerRow();
                } else if (hitBox(u.btnDroneSur, tx, ty)) {
                    engine.setDroneSwaraMode(engine.getDroneSwaraMode() + 1);
                    drawDroneAndTunerRow();
                } else if (hitBox(u.btnRndTala, tx, ty)) {
                    engine.randomizeTala();
                    engine.rebuildActivePatterns();
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    drawTopTransportRow();
                    drawTalometerLEDs();
                    drawFooter();
                } else if (hitBox(u.btnRndSong, tx, ty)) {
                    engine.randomizeRaga();
                    engine.rebuildActivePatterns();
                    engine.setHarmoniumPresetMode(true);
                    if (!engine.isPlaying()) engine.startWithCountdown(g_useCountdown);
                    drawHarmoniumDspRow();
                    drawUtilityRow();
                    drawTrackButtons();
                } else if (hitBox(u.btnMidiMode, tx, ty)) {
                    engine.setMidiInMode(engine.getMidiInMode() + 1);
                    drawUtilityRow();
                } else if (engine.hasSingingData() && hitBox(u.btnSingMode, tx, ty)) {
                    engine.toggleSingingEnabled();
                    drawUtilityRow();
                    drawTrackButtons();
                } else if (hitBox(u.btnHarmMode, tx, ty)) {
                    engine.setHarmoniumPresetMode(!engine.isHarmoniumPresetMode());
                    drawHarmoniumDspRow();
                } else if (hitBox(u.btnOctDown, tx, ty)) {
                    engine.setOctaveShift(engine.getOctaveShift() - 1);
                    drawHarmoniumDspRow();
                } else if (hitBox(u.btnOctUp, tx, ty)) {
                    engine.setOctaveShift(engine.getOctaveShift() + 1);
                    drawHarmoniumDspRow();
                } else if (hitBox(u.btnTfEn, tx, ty)) {
                    engine.setTfEnabled(!engine.isTfEnabled());
                    drawHarmoniumDspRow();
                } else if (hitBox(u.btnEditMode, tx, ty)) {
                    g_modalState = MODAL_HARM_CFG;
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
            if (hitBox(u.btnEdPrev, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.selectTala(engine.getSelectedTala() - 1);
                else engine.selectHarmoniumSong(engine.getSelectedSong() - 1);
                redrawEntireScreen();
            } else if (hitBox(u.btnEdNext, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.selectTala(engine.getSelectedTala() + 1);
                else engine.selectHarmoniumSong(engine.getSelectedSong() + 1);
                redrawEntireScreen();
            } else if (hitBox(u.btnEdRand, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) engine.randomizeTala();
                else engine.randomizeRaga();
                engine.rebuildActivePatterns();
                redrawEntireScreen();
            } else if (hitBox(u.btnEdPlay, tx, ty)) {
                if (engine.isPlaying()) engine.stopPlayback();
                else {
                    if (g_currentView == VIEW_HARM_ED) engine.setHarmoniumPresetMode(true);
                    engine.startWithCountdown(false);
                }
                redrawEntireScreen();
            } else if (hitBox(u.btnEdSave, tx, ty)) {
                if (g_currentView == VIEW_TALA_ED) g_kbdInput = "Tala_" + std::string(BuiltInPresets::getTalaThekas()[engine.getSelectedTala()].name);
                else g_kbdInput = "Raga_" + std::string(BuiltInPresets::getHarmoniumSongs()[engine.getSelectedSong()].title);
                std::replace(g_kbdInput.begin(), g_kbdInput.end(), ' ', '_');
                if (g_kbdInput.length() > 20) g_kbdInput = g_kbdInput.substr(0, 20);
                g_modalState = MODAL_SAVE_KBD;
                redrawEntireScreen();
            } else if (hitBox(u.btnEdLoad, tx, ty)) {
                g_fileList = listBankFiles();
                g_modalState = MODAL_FILE_LOAD;
                redrawEntireScreen();
            } else if (hitBox(u.btnEdDivs, tx, ty)) {
                auto cycleDivs = [](const char* cur) {
                    for (int i = 0; i < 6; i++) {
                        if (std::strcmp(cur, VALID_DIVISIONS[i]) == 0) return VALID_DIVISIONS[(i + 1) % 6];
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
                    int padX  = u.compactLabels ? 4 : 16;
                    int stepX = (u.scrW - padX * 2) / 8;
                    int cellW = stepX - 2;
                    int hdrH  = u.compactLabels ? 16 : 24;
                    int bolH  = u.compactLabels ? 24 : 36;
                    int karH  = u.compactLabels ? 24 : 36;
                    int rowH  = hdrH + bolH + karH + (u.compactLabels ? 6 : 24);

                    int globalMatra = 0;
                    for (size_t v = 0; v < vibhags.size(); ++v) {
                        for (int m = 0; m < vibhags[v]; ++m) {
                            int r = globalMatra / 8;
                            int c = globalMatra % 8;
                            int bx = padX + c * stepX;
                            int by = u.edGridY + r * rowH;

                            TouchButton bolBtn = {bx, by + hdrH + 1, cellW, bolH};
                            TouchButton karBtn = {bx, by + hdrH + bolH + 2, cellW, karH};

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
                        if (totalMatras <= 0) totalMatras = 8;

                        int padX  = u.compactLabels ? 4 : 16;
                        int maxCW = u.compactLabels ? 36 : 64;
                        int cellW = std::min(maxCW, (u.scrW - padX * 2) / totalMatras);
                        int hdrH  = u.compactLabels ? 18 : 24;
                        int rowH  = u.compactLabels ? 28 : 42;
                        int btnH  = rowH - (u.compactLabels ? 3 : 4);
                        int rowsY = u.edGridY + hdrH + (u.compactLabels ? 3 : 6);

                        for (int row = 0; row < hs.forms[0].numRows; ++row) {
                            for (int col = 0; col < totalMatras; ++col) {
                                TouchButton sBtn = {padX + col * cellW, rowsY + row * rowH, cellW - 1, btnH};
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

    // Live view periodic updates
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

    // Instrument Test Screen periodic updates (ONLY runs when VIEW_INST_TEST is active)
    if (g_currentView == VIEW_INST_TEST && g_modalState == MODAL_NONE) {
        if (g_testAutoCycle) {
            if (++g_testAutoTicks >= 48) {
                g_testAutoTicks = 0;
                int prevInst = g_testSelectedInst;
                g_testSelectedInst = (g_testSelectedInst + 1) % SynthEngine::NUM_TRACKS;
                triggerAndCaptureTestInstrument(g_testSelectedInst);
                if (getInstTestRects().miniTrig.w > 0) {
                    drawInstTestHeaderAndButtons();
                } else {
                    drawSingleTestInstButton(prevInst);
                    drawSingleTestInstButton(g_testSelectedInst);
                }
            }
        }
        if ((++g_testFrameDiv & 1) == 0) {
            updateInstTestVisualizers();
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
    pin_cfg.bck_io_num   = g_pins.i2sBclk;
    pin_cfg.ws_io_num    = g_pins.i2sLrck;
    pin_cfg.data_out_num = g_pins.i2sDout;
    pin_cfg.data_in_num  = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

static void masterAudioTask(void*) {
    float monoBuf[256];
    int16_t stereoBuf[512];

    while (true) {
        engine.renderFramesMono(monoBuf, 256);

        if (engine.isTestScopeEnabled()) {
            captureTestAudioFrames(monoBuf, 256);
        }

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

        taskYIELD();
    }
}

void setup() {
    Serial.begin(115200);
    initFileSystem();
    loadGpioConfig();
    applyI2CPins();

    int dummyTala = 0, dummySong = 0;
    loadBankFromFile("/Default_Bank.mhb", dummyTala, dummySong);

    WiFi.persistent(false);
    WifiAudioStreamer::instance().begin(&engine);

    initWaveshare7InchBacklightAndTouch();
    g_display.init();
#if SCREEN_PROFILE == 0
    g_display.setRotation(0);
    g_display.setFont(&fonts::Font2);
#else
    g_display.setFont(&fonts::Font0);
#endif
    g_display.setAttribute(UTF8_SWITCH, true);
    g_display.fillScreen(rgb565(22, 25, 30));

    g_display.setTextDatum(MC_DATUM);
    g_display.setTextColor(rgb565(0, 229, 255));
    g_display.drawString("Synthesizing Percussion into PSRAM...",
                         g_display.screenWidth() / 2, g_display.screenHeight() / 2);

    engine.init(AUDIO_SRATE);
    loadHarmoniumConfig();
    if (!engine.isTrackMuted(20)) engine.toggleTrackMuted(20); // Mute Shankha
    if (!engine.isTrackMuted(21)) engine.toggleTrackMuted(21); // Mute Shankha 2
    initI2S();

    g_usbAudio.begin();
    usbMidi.begin([](int note, int vel, bool on) {
        engine.handleIncomingMidi(note, vel, on);
    });

    redrawEntireScreen();

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
        loadHarmoniumConfig();

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

        if (engine.isTestScopeEnabled()) {
            captureTestAudioFrames(m_monoScratch.data(), frames);
        }

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

    display.onProfileChanged = [](int) {
        g_modalState = MODAL_NONE;
        redrawEntireScreen();
    };

    display.onPcKeyMidi = [](int midi, bool on) {
        engine.handleIncomingMidi(midi, 105, on);
        if (g_currentView == VIEW_LIVE && g_modalState == MODAL_NONE &&
            (engine.getMidiInMode() == 0 || engine.getMidiInMode() == 2 || (engine.getMidiInMode() == 1 && midi >= 52))) {
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
            if (g_currentView == VIEW_LIVE && g_modalState == MODAL_NONE &&
                (engine.getMidiInMode() == 0 || engine.getMidiInMode() == 2 || (engine.getMidiInMode() == 1 && note >= 52))) {
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