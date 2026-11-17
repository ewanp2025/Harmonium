#ifndef DRUM_PRESETS_H
#define DRUM_PRESETS_H

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>

struct SongNote {
    int startTick;
    int lenTicks;
    int key;
    float vol;
    float a1; 
    float a2; 
    float a3;

    SongNote(int s = 0, int l = 48, int k = 69, float v = 1.0f,
             float pA1 = 0.0f, float pA2 = 0.2f, float pA3 = 0.5f)
        : startTick(s), lenTicks(l), key(k), vol(v), a1(pA1), a2(pA2), a3(pA3) {}
};

struct TrackSpec {
    const char* name;
    bool isBeatTrack;
    bool muted;
    const char* o1Str;
    const char* o2Str;
    bool useAdsr;
    float dec, sus, rel;
};

struct TalaTheka {
    const char* name;
    const char* divisions;
    std::vector<const char*> bolCells;
    std::vector<const char*> kartalCells;
};

struct SongForm {
    const char* formName;
    int numRows;
    std::vector<const char*> swaraCells;
    std::vector<const char*> lyricCells = {};
};

struct HarmoniumSong {
    const char* title;
    const char* ragaName;
    const char* divisions;
    int defaultBpm;
    std::vector<SongForm> forms;
    const char* lyricsUrl = nullptr;
    std::vector<const char*> lyrics = {};
};

namespace BuiltInPresets {

inline const char* defaultHarmoniumO1() {
    return "(1 - exp(-t * 8)) * exp(-trel * 18) * (0.82 + 0.18 * sinew(integrate(tempo / 120))) * ( "
           "(sinew(integrate(f * cent(-4 * sinew(integrate(tempo / 120))))) - sinew(2 * integrate(f))/2 + "
           "(0.75 + 0.25 * sinew(integrate(tempo / 120))) * (sinew(3 * integrate(f))/3 - sinew(4 * integrate(f))/4 + sinew(5 * integrate(f))/5)) + "
           "0.45 * (sinew(integrate(f * cent(8))) - sinew(2 * integrate(f * cent(8)))/2 + sinew(3 * integrate(f * cent(8)))/3) + "
           "0.65 * (sinew(integrate(f / 2)) - sinew(2 * integrate(f / 2))/2 + sinew(3 * integrate(f / 2))/3) )";
}

inline const char* defaultSingingVoiceO1() {

    return "exp(-trel * 16) * ( 0.7 * ( "
           "exp(-251.3 * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) * "
           "sinew((0.8 + 0.4 * A3) * (800 - 270 * clamp(0, 4 * A1, 1) - 260 * clamp(0, 4 * A1 - 1, 1) + 230 * clamp(0, 4 * A1 - 2, 1) - 200 * clamp(0, 4 * A1 - 3, 1)) * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) "
           "+ "
           "0.45 * exp(-282.7 * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) * "
           "sinew((0.8 + 0.4 * A3) * (1200 + 640 * clamp(0, 4 * A1, 1) + 460 * clamp(0, 4 * A1 - 1, 1) - 1400 * clamp(0, 4 * A1 - 2, 1) - 100 * clamp(0, 4 * A1 - 3, 1)) * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) "
           "+ "
           "0.22 * exp(-377.0 * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) * "
           "sinew((0.8 + 0.4 * A3) * (2500 + 500 * clamp(0, 4 * A1 - 1, 1) - 600 * clamp(0, 4 * A1 - 2, 1) - 200 * clamp(0, 4 * A1 - 3, 1)) * (mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1) / f)) "
           ") * pow(1 - mod(integrate(f * (1 - 0.025 * exp(-20 * t) + 0.008 * (1 - exp(-4 * t)) * sinew(5.5 * t))), 1), 2) * (1 - exp(-30 * t)) "
           "+ A2 * 0.45 * exp(-40 * t) * sinew((2500 + 2000 * A3) * t + 17.3 * sinew(7919 * t + 11.7 * sinew(13411 * t))) )";
}

inline const char* shrutiDroneO1() {
    return "0.3 * (0.55*saww(integrate(f*cent(-5))) + 0.35*squarew(integrate(f*cent(4))) + 0.2*sinew(integrate(f*2))) * "
           "(0.92 + 0.08*sinew(t*tempo/60)) * min(1, t*15) * ((1 - rel) + rel*exp(-trel*6))";
}
inline const char* shrutiDroneO2() {
    return "0.3 * (0.55*saww(integrate(f*cent(5))) + 0.35*squarew(integrate(f*cent(-4))) + 0.2*sinew(integrate(f*2))) * "
           "(0.92 + 0.08*sinew(t*tempo/60)) * min(1, t*15) * ((1 - rel) + rel*exp(-trel*6))";
}

inline const char* tanpuraDroneO1() {
    return "0.35 * (sinew(integrate(f) + (0.6 + 0.35*sinew(t*tempo/240))*sinew(integrate(f*2)) + 0.25*sinew(integrate(f*3))) + "
           "0.4*saww(integrate(f*cent(-3)))*exp(-mod(t*tempo/120, 1)*2.2)) * min(1, t*20) * ((1 - rel) + rel*exp(-trel*3))";
}
inline const char* tanpuraDroneO2() {
    return "0.35 * (sinew(integrate(f*cent(3)) + (0.6 + 0.35*cos(t*tempo/240))*sinew(integrate(f*2)) + 0.25*sinew(integrate(f*3))) + "
           "0.4*saww(integrate(f))*exp(-mod(t*tempo/120 + 0.5, 1)*2.2)) * min(1, t*20) * ((1 - rel) + rel*exp(-trel*3))";
}

inline const std::vector<TrackSpec>& getTracks() {
    static const std::vector<TrackSpec> tracks = {
        {"Dha", true, false,
         "0.65 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.2 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.2 * exp(-10 * t) * sin(2 * pi * f * 1.1892 * t) + 0.15 * exp(-75 * t) * sin(2 * pi * 1800 * t)",
         "0.35 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.45 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.3 * exp(-14 * t) * sin(2 * pi * f * 1.1892 * t) + 0.25 * exp(-75 * t) * sin(2 * pi * 1800 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Dhin", true, false,
         "0.65 * exp(-4.5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.5 * exp(-20 * t))) + 0.25 * exp(-7 * t) * sin(2 * pi * f * 0.5946 * t) + 0.15 * exp(-9 * t) * sin(2 * pi * f * 1.1892 * t)",
         "0.35 * exp(-4.5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.5 * exp(-20 * t))) + 0.5 * exp(-6.5 * t) * sin(2 * pi * (f * 0.5946 * t - 0.08 * exp(-35 * t))) + 0.35 * exp(-9 * t) * sin(2 * pi * f * 1.1892 * t) + 0.2 * exp(-14 * t) * sin(2 * pi * f * 1.7838 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Ge", true, false,
         "0.75 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.25 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.2 * exp(-60 * t) * sin(2 * pi * 280 * t)",
         "0.5 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.15 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.12 * exp(-60 * t) * sin(2 * pi * 280 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Ka", true, false,
         "0.6 * exp(-35 * t) * sin(2 * pi * (160 * t - 0.4 * exp(-50 * t))) + 0.35 * exp(-90 * t) * sin(2 * pi * 420 * t) + 0.25 * exp(-140 * t) * sin(2 * pi * 950 * t)",
         "0.4 * exp(-35 * t) * sin(2 * pi * (160 * t - 0.4 * exp(-50 * t))) + 0.22 * exp(-90 * t) * sin(2 * pi * 420 * t) + 0.15 * exp(-140 * t) * sin(2 * pi * 950 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Na", true, false,
         "0.35 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.3 * exp(-12 * t) * sin(2 * pi * f * 1.1892 * t) + 0.15 * exp(-22 * t) * sin(2 * pi * f * 1.7838 * t) + 0.18 * exp(-85 * t) * sin(2 * pi * f * 3.5 * t)",
         "0.55 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.45 * exp(-12 * t) * sin(2 * pi * f * 1.1892 * t) + 0.22 * exp(-22 * t) * sin(2 * pi * f * 1.7838 * t) + 0.28 * exp(-85 * t) * sin(2 * pi * f * 3.5 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Ta", true, false,
         "0.4 * exp(-14 * t) * sin(2 * pi * (f * 0.5946 * t - 0.18 * exp(-45 * t))) + 0.2 * exp(-20 * t) * sin(2 * pi * f * 1.1892 * t) + 0.25 * exp(-90 * t) * sin(2 * pi * 1800 * t)",
         "0.65 * exp(-14 * t) * sin(2 * pi * (f * 0.5946 * t - 0.18 * exp(-45 * t))) + 0.3 * exp(-20 * t) * sin(2 * pi * f * 1.1892 * t) + 0.38 * exp(-90 * t) * sin(2 * pi * 1800 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Ti", true, false,
         "0.32 * exp(-85 * t) * sin(2 * pi * 820 * t) + 0.25 * exp(-130 * t) * sin(2 * pi * 1350 * t) + 0.18 * exp(-220 * t) * sin(2 * pi * 2500 * t)",
         "0.5 * exp(-85 * t) * sin(2 * pi * 820 * t) + 0.4 * exp(-130 * t) * sin(2 * pi * 1350 * t) + 0.3 * exp(-220 * t) * sin(2 * pi * 2500 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Re", true, false,
         "0.30 * exp(-65 * t) * sin(2 * pi * 900 * t) + 0.22 * exp(-110 * t) * sin(2 * pi * 1480 * t) + 0.16 * exp(-180 * t) * sin(2 * pi * 2750 * t)",
         "0.48 * exp(-65 * t) * sin(2 * pi * 900 * t) + 0.38 * exp(-110 * t) * sin(2 * pi * 1480 * t) + 0.26 * exp(-180 * t) * sin(2 * pi * 2750 * t)",
         false, 0.5f, 0.5f, 0.1f},
        {"Kartal Open", true, false,
         "0.45 * exp(-3 * t) * sin(2 * pi * 3120 * t + 1.5 * exp(-8 * t) * sin(2 * pi * 4412 * t)) + 0.35 * exp(-4.5 * t) * sin(2 * pi * 4850 * t + 1.2 * exp(-12 * t) * sin(2 * pi * 6858 * t)) + 0.3 * exp(-70 * t) * sin(2 * pi * 7200 * t + 4 * sin(2 * pi * 5100 * t))",
         "0.45 * exp(-2.8 * t) * sin(2 * pi * 3126 * t + 1.5 * exp(-8 * t) * sin(2 * pi * 4420 * t)) + 0.35 * exp(-4.2 * t) * sin(2 * pi * 4858 * t + 1.2 * exp(-12 * t) * sin(2 * pi * 6866 * t)) + 0.3 * exp(-70 * t) * sin(2 * pi * 7215 * t + 4 * sin(2 * pi * 5110 * t))",
         false, 0.5f, 0.5f, 0.1f},
        {"Kartal Closed", true, false,
         "0.5 * exp(-40 * t) * sin(2 * pi * 3120 * t + 3 * exp(-60 * t) * sin(2 * pi * 4412 * t)) + 0.4 * exp(-55 * t) * sin(2 * pi * 4850 * t + 3.5 * exp(-70 * t) * sin(2 * pi * 6858 * t)) + 0.4 * exp(-100 * t) * sin(2 * pi * 7500 * t + 6 * sin(2 * pi * 5300 * t))",
         "0.5 * exp(-40 * t) * sin(2 * pi * 3126 * t + 3 * exp(-60 * t) * sin(2 * pi * 4420 * t)) + 0.4 * exp(-55 * t) * sin(2 * pi * 4858 * t + 3.5 * exp(-70 * t) * sin(2 * pi * 6866 * t)) + 0.4 * exp(-100 * t) * sin(2 * pi * 7515 * t + 6 * sin(2 * pi * 5310 * t))",
         false, 0.5f, 0.5f, 0.1f},
        {"Ghanta", true, false,
         "clamp(-1, 0.35 * ( 0.45 * exp(-0.35 * t) * (sin(6.28318 * 0.5 * f * t) + 0.8 * sin(6.28318 * (0.5 * f + 0.8 + A2 * 1.5) * t)) + 0.90 * exp(-1.1 * t) * (sin(6.28318 * f * t + 1.8 * exp(-35 * t) * sin(6.28318 * 2.756 * f * t)) + 0.6 * sin(6.28318 * (f * 1.0035) * t)) + 0.55 * exp(-1.9 * t) * sin(6.28318 * 1.183 * f * t) + 0.70 * exp(-2.4 * t) * (sin(6.28318 * 2.0 * f * t) + 0.5 * sin(6.28318 * (2.0025 * f) * t)) + 0.45 * exp(-4.5 * t) * sin(6.28318 * 2.756 * f * t) + 0.30 * exp(-8.0 * t) * sin(6.28318 * 4.08 * f * t) + 0.20 * exp(-14.0 * t) * sin(6.28318 * 5.41 * f * t) + 0.35 * exp(-80 * t) * randv(t * srate) * (0.4 + 0.6 * sin(6.28318 * 3200 * t)) ), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Kansa", true, false,
         "clamp(-1, 0.38 * ( 0.85 * exp(-0.9 * t) * sin(6.28318 * f * (1 + 0.025 * exp(-12 * t)) * t + 0.8 * exp(-2.5 * t) * sin(6.28318 * 1.618 * f * t)) + 0.65 * exp(-1.4 * t) * sin(6.28318 * 1.414 * f * t + (2.5 + A1 * 2.0) * (1 - exp(-8 * t)) * exp(-1.8 * t) * sin(6.28318 * 2.414 * f * t)) + 0.50 * (1 - exp(-6 * t)) * exp(-1.6 * t) * sin(6.28318 * 2.236 * f * t + (3.5 + A2 * 3.0) * (1 - exp(-5 * t)) * exp(-2.2 * t) * sin(6.28318 * 3.618 * f * t)) + 0.35 * (1 - exp(-4 * t)) * exp(-2.5 * t) * sin(6.28318 * 3.162 * f * t) + (0.18 * (1 - exp(-5 * t)) * exp(-2.0 * t) + 0.25 * exp(-60 * t)) * randv(t * srate) * sin(6.28318 * f * 4.5 * t) ) + 0.22 * last(1), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Manjira", true, false,
         "clamp(-1, 0.40 * ( 0.70 * exp(-3.5 * t) * (sin(6.28318 * (f * 4.0) * t) + 0.8 * sin(6.28318 * (f * 4.015) * t)) + 0.55 * exp(-5.0 * t) * sin(6.28318 * (f * 5.38) * t + 2.2 * exp(-15 * t) * sin(6.28318 * (f * 7.12) * t)) + 0.40 * exp(-8.0 * t) * sin(6.28318 * (f * 6.85) * t) + 0.30 * exp(-18.0 * t) * sin(6.28318 * (f * 9.20) * t) + (0.45 * exp(-120 * t) + 0.12 * exp(-12 * t)) * randv(t * srate) * (0.3 + 0.7 * sin(6.28318 * 6500 * t)) ), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Damaru", true, false,
         "clamp(-1, last(1) + clamp(0.01, 6.28318 * (f * 3.5 + 800 * exp(-35 * t) + A1 * 1000) / srate, 0.98) * ( ( 0.90 * exp(-7.5 * t) * sinew(integrate(f * (1 + (0.45 + A2 * 0.4) * exp(-28 * t)))) + 0.45 * exp(-14.0 * t) * sinew(integrate(1.593 * f * (1 + 0.30 * exp(-35 * t)))) + 0.25 * exp(-22.0 * t) * sinew(integrate(2.135 * f * (1 + 0.20 * exp(-40 * t)))) + (0.55 * exp(-90 * t) + 0.35 * exp(-70 * mod(t, 0.125 + A3 * 0.08)) * (t < 0.38)) * randv(t * srate) * (0.4 + 0.6 * sin(6.28318 * 1400 * t)) ) - last(1) ), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Ra", true, false,
         "0.5 * exp(-60 * t) * randv(t * srate) * sin(2 * pi * 400 * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Ki", true, false,
         "0.6 * exp(-70 * t) * randv(t * srate) * sin(2 * pi * 600 * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Tabla_Na", true, false,
         "0.8 * exp(-30 * t) * sin(2 * pi * 850 * t) + 0.4 * exp(-80 * t) * sin(2 * pi * 1500 * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Dagga_Ge", true, false,
         "0.9 * exp(-4 * t) * sin(2 * pi * (110 + 40 * exp(-10 * t)) * t) + 0.3 * exp(-20 * t) * sin(2 * pi * 220 * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Khol_High", true, false,
         "0.7 * exp(-40 * t) * sin(2 * pi * 1200 * t) + 0.4 * exp(-100 * t) * sin(2 * pi * 2800 * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Khol_Low", true, false,
         "0.8 * exp(-3 * t) * sin(2 * pi * (85 + 20 * exp(-8 * t)) * t)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Shankha", false, true,
         "clamp(-1, last(1) + clamp(0.01, 6.28318 * f * (2.2 + A1 * 2.0) / srate, 0.92) * ( 0.28 * ( (1 - exp(-10 * t)) * exp(-0.16 * t) * ( sin(6.28318 * f * (1 - 0.18 * exp(-15 * t)) * t) + 0.65 * sin(12.56636 * f * (1 - 0.18 * exp(-15 * t)) * t) + 0.40 * (1 - exp(-4 * t)) * sin(18.84955 * f * (1 - 0.18 * exp(-15 * t)) * t) ) + ((t > 0.06) * (1 - exp(-8 * (t - 0.06))) * exp(-0.15 * (t - 0.06)) * ( 0.85 * sin(6.28318 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) + 0.75 * sin(12.56636 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) + 0.55 * (1 - exp(-3.5 * (t - 0.06))) * sin(18.84955 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) )) + (0.10 * exp(-5 * t) + 0.025) * randv(t * srate) ) - last(1) ), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Shankha 2", false,true,
         "clamp(-1, last(1) + clamp(0.02, 6.28318 * (1200 + f * 1.5 + A1 * 800) / srate, 0.96) * ( (1 - exp(-18 * t)) * exp(-0.08 * t) * (0.88 + 0.12 * sinew(5.5 * t)) * ( 0.45 * saww(integrate(f * (1 - 0.08 * exp(-25 * t)) * (1 + (0.018 + A2 * 0.04) * sinew(5.8 * t)))) + 0.35 * squarew(integrate(f * 1.001 * (1 - 0.08 * exp(-25 * t)) * (1 + (0.018 + A2 * 0.04) * sinew(5.8 * t)))) + 0.30 * sin(6.28318 * 2.0 * f * (1 + (0.018 + A2 * 0.04) * sin(6.28318 * 5.8 * t)) * t) + 0.25 * sin(6.28318 * 3.0 * f * (1 + (0.018 + A2 * 0.04) * sin(6.28318 * 5.8 * t)) * t) + (0.09 * exp(-15 * t) + 0.04) * randv(t * srate) * sin(6.28318 * 1800 * t) ) - last(1) ), 1)",
         "", false, 0.5f, 0.5f, 0.1f},
        {"Harmonium_Bellows", false, false,
         defaultHarmoniumO1(), "", true, 2.0f, 1.0f, 0.348f},
        {"Singing_Voice", false, true,
         defaultSingingVoiceO1(), "", true, 2.0f, 1.0f, 0.15f}
    };
    return tracks;
}

inline std::vector<TalaTheka>& getTalaThekas() {
    static std::vector<TalaTheka> talas = {
        {"Kaherva (8 Matra)", "4+4",
         {"Dha", "Ge", "Na", "Ti", "Na", "Ka", "Dhin", "Na"},
         {"Kartal Open", "-", "Kartal Closed", "Kartal Closed", "Kartal Open", "-", "Kartal Closed", "Kartal Closed"}},
        {"Prabhupada (8 Matra)", "4+2+2",
         {"Dha Ti Ra Ki", "Ta Ta Ta Ti", "Ra Ki Ta Ta", "Ka Ti Ra Ki", "Ta Dha Dhin -", "Dha - Dhin -", "Dha - - -", "- - - -"},
         {"Kartal Open", "-", "-", "-", "Kartal Closed", "-", "Kartal Open", "-"}},
        {"Kaherva Prakar 1 (8 Matra)", "4+4",
         {"Dha", "Ti", "Ti", "Ta", "Ta", "Dhin", "Dhin", "Dha"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed"}},
        {"Kaherva Prakar 2 (8 Matra)", "4+4",
         {"Ge", "-", "Ti", "Ta", "-", "Ge", "Dhin", "Na"},
         {"Kartal Open", "-", "Kartal Closed", "Kartal Closed", "Kartal Open", "-", "Kartal Closed", "Kartal Closed"}},
        {"Kaherva Prakar 3 (8 Matra)", "4+4",
         {"Dha", "Ti Re", "Ka Ti", "Ta", "Ge", "Dhin", "Dhin", "Dha"},
         {"Kartal Open", "Closed+Closed", "Closed+Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed"}},
        {"Kaherva Prakar 4 (8 Matra)", "4+4",
         {"Dhin", "Ta", "Ti", "Ta", "Ti", "Ta", "Dhin", "Ta"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed"}},
        {"Bhajani (8 Matra)", "4+4",
         {"Dhin", "Ta Dhin", "Dhin", "Ta", "Ti", "Ta Ti", "Ti", "Ta"},
         {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed"}},
        {"Dhumali (8 Matra)", "4+4",
         {"Dha", "Dhin", "Dha", "Ta", "Ti Re Ka Ta", "Dhin", "Dhin", "Ge"},
         {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Closed+Closed", "Kartal Open", "Kartal Closed", "Kartal Closed"}},
        {"Dadra (6 Matra)", "3+3",
         {"Dha", "Dhin", "Na", "Dha", "Ti", "Na"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed"}},
        {"Khemta (6 Matra)", "3+3",
         {"Dha", "Ti", "Na", "Na", "Ka", "Ta"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed"}},
        {"Rupak (7 Matra)", "3+2+2",
         {"Ti", "Ti", "Na", "Dhin", "Na", "Dhin", "Na"},
         {"Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed"}},
        {"Tivra (7 Matra)", "3+2+2",
         {"Dha", "Dhin", "Ta", "Ti", "Ta", "Ka", "Ta"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed"}},
        {"Matta (9 Matra)", "2+4+3",
         {"Dha", "Ge", "Ti", "Ka", "Ti", "Ta", "Dhin", "Dhin", "Ta"},
         {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Closed", "-", "-"}},
        {"Jhaptal (10 Matra)", "2+3+2+3",
         {"Dhin", "Na", "Dhin", "Dhin", "Na", "Ti", "Na", "Dhin", "Dhin", "Na"},
         {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "-", "Kartal Open", "Kartal Closed", "Kartal Closed"}},
        {"Ektal (12 Matra)", "2+2+2+2+2+2",
         {"Dhin", "Dhin", "Dha Ge", "Ti Re Ka Ta", "Ti", "Na", "Ka", "Ta", "Dha Ge", "Ti Re Ka Ta", "Dhin", "Na"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "-", "Kartal Open", "Kartal Closed", "Kartal Closed", "-", "Kartal Open", "Closed+Closed", "Kartal Open", "Kartal Closed"}},
        {"Tintal (16 Matra)", "4+4+4+4",
         {"Dha", "Dhin", "Dhin", "Dha", "Dha", "Dhin", "Dhin", "Dha", "Dha", "Ti", "Ti", "Ta", "Ta", "Dhin", "Dhin", "Dha"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Closed", "-", "-", "-", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed"}},
        {"Tintal Vilambit (16 Matra)", "4+4+4+4",
         {"Dha", "Dhin", "Dhin", "Ka Dha", "Dha", "Dhin", "Dhin", "Ka Dha", "Dha", "Ti", "Ti", "Ta", "Ta", "Dhin", "Dhin", "Ka Dha"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Closed+Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Closed+Closed", "Kartal Closed", "-", "-", "-", "Kartal Open", "Kartal Closed", "Kartal Closed", "Closed+Closed"}},
        {"Tintal Drut (16 Matra)", "4+4+4+4",
         {"Dha", "Ge Ti", "Dha", "Ge Ti", "Dha", "Ge Ti", "Dha", "Ge Ti", "Ta", "Ka Ti", "Ta", "Ka Ti", "Dha", "Ge Ti", "Dha", "Ge Ti"},
         {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Closed", "-", "Kartal Closed", "-", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed"}},
        {"Sitarkhani (16 Matra)", "4+4+4+4",
         {"Dha", "Ge", "Dhin", "Ge", "Dha", "Ge", "Dhin", "Ge", "Dha", "Ka", "Ti", "Ka", "Ta", "Ge", "Dhin", "Ge"},
         {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "-", "Kartal Closed", "-", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed"}}
    };
    return talas;
}

inline const std::vector<HarmoniumSong>& getHarmoniumSongs() {
    static const std::vector<HarmoniumSong> songs = {
        {"Raga Asavari - Dhun 1", "Asavari", "4+4", 130, {
                                                             {"Form A (Sthayi)", 4, {"S","R","g","-","R","-","S","'n","S","-","R","m","P","-","d","P","m","P","d","n","d","P","m","g","R","g","R","S","'n","S","-","-"}},
                                                             {"Form B (Antara)", 4, {"m","P","d","n","S'","-","S'","-","n","S'","R'","S'","n","d","P","-","m","P","d","P","m","g","R","g","R","-","S","'n","S","-","-","-"}}
                                                         }},
        {"Raga Asavari - Dhun 2 (12M)", "Asavari", "4+4+4", 132, {
                                                                     {"Form A (Sthayi)", 2, {"S","r","g","m","P","-","d","P","m","g","r","S","r","g","m","P","m","g","r","'n","S","-","-","-"}},
                                                                     {"Form B (Antara)", 2, {"m","P","d","n","S'","-","S'","n","d","P","m","P","d","P","m","g","r","g","r","S","S","-","-","-"}}
                                                                 }},
        {"Raga Asavari - Dhun 3", "Asavari", "4+4", 128, {
                                                             {"Form A (Sthayi)", 4, {"S","R","m","P","d","-","P","-","m","P","d","P","m","g","R","S","S","R","g","R","S","'n","'d","'n","S","-","R","S","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"m","P","d","S'","S'","-","S'","-","n","S'","R'","g'","R'","S'","n","d","P","d","P","m","g","R","g","R","S","'n","S","-","S","-","-","-"}}
                                                         }},
        {"Raga Darbari Kanhada", "Darbari Kanhada", "4+4", 125, {
                                                                    {"Form A (Sthayi)", 4, {"'d","'n","S","-","S","'n","S","R","g","-","R","S","'n","'d","'n","S","S","R","m","-","m","P","d","P","d","P","m","g","R","S","-","-"}},
                                                                    {"Form B (Antara)", 4, {"m","P","d","n","S'","-","S'","-","n","S'","R'","g'","R'","S'","n","d","P","d","n","d","P","m","g","R","g","R","S","'n","S","-","-","-"}}
                                                                }},
        {"Raga Bangala - Bandish", "Bangala", "4+4", 132, {
                                                              {"Form A (Sthayi)", 4, {".","S","R","G","G","-","G","m","G","R","S","R","G","-","-","-","G","m","P","P","P","-","D","P","m","G","R","S","S","-","-","-"}},
                                                              {"Form B (Antara)", 4, {".","P","P","D","S'","-","S'","S'","N","S'","R'","S'","N","D","P","-","R","D","D","D","P","-","m","G","m","G","R","S","S","-","-","-"}}
                                                          }},
        {"Raga Bilawal - Dhun 1", "Bilawal", "4+4", 138, {
                                                             {"Form A (Sthayi)", 4, {"S","R","G","-","G","m","P","m","G","-","R","-","S","'N","S","-","S","R","G","m","P","-","D","P","m","G","R","-","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"G","P","P","D","S'","-","S'","-","S'","R'","G'","R'","S'","N","D","P","P","D","P","m","G","m","P","m","G","-","R","'N","S","-","-","-"}}
                                                         }},
        {"Raga Bilawal - Dhun 2", "Bilawal", "4+4", 136, {
                                                             {"Form A (Sthayi)", 4, {"S","G","G","G","G","m","G","R","S","R","G","m","P","-","P","-","G","m","P","D","P","m","G","R","S","R","G","R","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"P","P","D","N","S'","-","S'","-","N","S'","R'","S'","N","D","P","-","G","m","P","D","P","m","G","R","S","R","G","R","S","-","-","-"}}
                                                         }},
        {"Raga Bilawal - Dhun 3", "Bilawal", "4+4", 134, {
                                                             {"Form A (Sthayi)", 4, {"S","S","R","R","G","-","m","G","R","G","m","P","m","G","R","S","S","R","G","m","P","D","P","m","G","m","G","R","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"m","P","D","N","S'","-","S'","-","S'","R'","G'","R'","S'","N","D","P","D","P","m","G","m","P","m","G","R","G","R","S","S","-","-","-"}}
                                                         }},
        {"Raga Kafi - Dhun 1", "Kafi", "4+4", 132, {
                                                       {"Form A (Sthayi)", 4, {"S","R","g","R","g","m","P","-","m","P","m","g","R","-","S","-","'n","S","R","g","m","g","R","S","R","g","R","'n","S","-","-","-"}},
                                                       {"Form B (Antara)", 4, {"m","P","D","n","S'","-","S'","-","n","S'","R'","S'","n","D","P","-","m","P","D","P","m","g","R","g","R","-","S","'n","S","-","-","-"}}
                                                   }},
        {"Raga Kafi - Dhun 2", "Kafi", "4+4", 135, {
                                                       {"Form A (Sthayi)", 4, {"S","S","g","-","m","m","P","-","m","P","D","n","D","P","m","g","R","g","m","P","m","g","R","'n","S","-","g","R","S","-","-","-"}},
                                                       {"Form B (Antara)", 4, {"m","P","n","n","S'","-","S'","-","n","S'","R'","g'","R'","S'","n","D","P","D","n","D","P","m","g","R","g","m","g","R","S","-","-","-"}}
                                                   }},
        {"Raga Kafi - Dadra Dhun", "Kafi", "3+3", 124, {
                                                           {"Form A (Sthayi)", 4, {"S","R","g","m","P","m","g","R","S","'n","S","-","R","g","m","P","D","P","m","g","R","S","-","-"}},
                                                           {"Form B (Antara)", 4, {"m","P","n","S'","-","S'","n","S'","R'","n","D","P","D","P","m","g","R","g","R","'n","S","S","-","-"}}
                                                       }},
        {"Raga Misra Kafi - Bandish", "Misra Kafi", "4+4", 128, {
                                                                    {"Form A (Sthayi)", 4, {"S","g","R","g","m","P","m","g","R","g","R","S","'n","S","R","G","m","P","D","n","D","P","m","g","R","g","R","S","S","-","-","-"}},
                                                                    {"Form B (Antara)", 4, {"m","P","D","N","S'","-","S'","-","n","S'","R'","g'","R'","S'","n","D","P","D","n","D","P","m","g","R","g","R","S","'n","S","-","-","-"}}
                                                                }},
        {"Raga Khamaja - Dhun 1", "Khamaja", "4+4", 138, {
                                                             {"Form A (Sthayi)", 4, {"S","G","m","P","D","n","D","P","m","G","m","G","R","S","'N","S","S","G","m","P","m","G","R","G","m","G","R","S","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"G","m","P","N","S'","-","S'","-","n","S'","R'","S'","n","D","P","-","G","m","P","D","n","D","P","m","G","m","G","R","S","-","-","-"}}
                                                         }},
        {"Raga Khamaja - Dhun 2", "Khamaja", "4+4", 132, {
                                                             {"Form A (Sthayi)", 4, {"G","m","P","D","n","D","P","m","G","m","P","m","G","R","S","-","S","G","m","P","m","G","R","S","'N","S","G","R","S","-","-","-"}},
                                                             {"Form B (Antara)", 4, {"m","P","D","N","S'","-","S'","-","N","S'","G'","R'","S'","n","D","P","D","n","D","P","m","G","R","G","m","G","R","S","S","-","-","-"}}
                                                         }},
        {"Raga Khamaja - Dadra Dhun", "Khamaja", "3+3", 130, {
                                                                 {"Form A (Sthayi)", 4, {"S","G","m","P","-","P","m","D","P","m","G","R","S","G","m","P","m","G","R","G","R","S","-","-"}},
                                                                 {"Form B (Antara)", 4, {"m","P","N","S'","-","S'","n","S'","R'","n","D","P","D","n","D","P","m","G","m","G","R","S","-","-"}}
                                                             }},
        {"Raga Jhinjhoti - Dhun 1", "Jhinjhoti", "4+4", 134, {
                                                                 {"Form A (Sthayi)", 4, {"'D","'n","S","R","G","m","G","R","S","R","G","m","P","-","m","G","R","G","m","P","D","n","D","P","m","G","R","S","S","-","-","-"}},
                                                                 {"Form B (Antara)", 4, {"m","P","D","n","S'","-","S'","-","n","S'","R'","G'","R'","S'","n","D","P","D","n","D","P","m","G","R","G","m","G","R","S","-","-","-"}}
                                                             }},
        {"Raga Jhinjhoti - Dadra", "Jhinjhoti", "3+3", 132, {
                                                                {"Form A (Sthayi)", 4, {"S","R","G","m","P","m","G","R","S","'n","'D","S","S","R","m","P","D","P","m","G","R","S","-","-"}},
                                                                {"Form B (Antara)", 4, {"m","P","D","S'","-","S'","n","S'","R'","n","D","P","D","n","D","P","m","G","R","G","R","S","-","-"}}
                                                            }},
        {"Raga Durga - Bandish", "Durga", "4+4", 136, {
                                                          {"Form A (Sthayi)", 4, {"S","R","m","-","m","P","D","P","m","P","D","m","R","-","S","-","S","R","m","P","D","-","P","m","R","m","R","S","S","-","-","-"}},
                                                          {"Form B (Antara)", 4, {"m","P","D","S'","S'","-","S'","-","S'","R'","m'","R'","S'","D","P","-","m","P","D","S'","D","P","m","R","S","R","m","R","S","-","-","-"}}
                                                      }},
        {"Raga Pahadi - Folk Dhun", "Pahadi", "4+4", 140, {
                                                              {"Form A (Sthayi)", 4, {"'P","'D","S","R","G","-","R","G","P","D","P","G","R","G","R","S","'P","'D","S","R","G","P","G","R","S","R","S","'D","S","-","-","-"}},
                                                              {"Form B (Antara)", 4, {"G","P","D","S'","S'","-","S'","-","S'","R'","G'","R'","S'","D","P","G","P","D","P","G","R","G","R","S","'D","S","R","S","S","-","-","-"}}
                                                          }},
        {"Raga Bhimpalasi - Bandish", "Bhimpalasi", "4+4", 135, {
                                                                    {"Form A (Sthayi)", 4, {"S","S","S","S","R","P","P","P","m","m","m","P","g","g","R","S","'n","S","g","m","P","-","g","m","g","-","R","S","S","-","-","-"}},
                                                                    {"Form B (Antara)", 4, {"g","m","P","n","S'","-","S'","-","n","S'","R'","S'","n","D","P","-","P","n","D","P","m","P","g","m","g","-","R","S","S","-","-","-"}}
                                                                }},
        {"Raga Sindhu Bhairavi Dadra", "Sindhu Bhairavi", "3+3", 126, {
                                                                          {"Form A (Sthayi)", 4, {"S","R","g","m","P","-","P","m","g","R","S","-","'n","S","R","g","m","g","R","S","'n","S","-","-"}},
                                                                          {"Form B (Antara)", 4, {"m","P","d","S'","-","S'","n","S'","r'","S'","n","d","P","d","P","m","g","R","g","R","S","S","-","-"}}
                                                                      }},
        {"Raga Sindhu Bhairavi Dhun", "Sindhu Bhairavi", "4+4", 132, {
                                                                         {"Form A (Sthayi)", 4, {"S","r","g","m","P","-","d","P","m","g","r","g","R","S","'n","S","S","R","g","m","P","m","g","r","S","r","S","'n","S","-","-","-"}},
                                                                         {"Form B (Antara)", 4, {"m","P","d","n","S'","-","S'","-","n","S'","R'","g'","r'","S'","n","d","P","d","P","m","g","r","g","R","S","'n","S","-","S","-","-","-"}}
                                                                     }},
        {"Raga Desh - Dadra Dhun 1", "Desh", "3+3", 140, {
                                                             {"Form A (Sthayi)", 4, {"S","R","m","P","N","D","P","m","G","R","G","S","R","m","P","D","m","P","G","R","'N","S","-","-"}},
                                                             {"Form B (Antara)", 4, {"m","P","N","S'","-","S'","N","S'","R'","n","D","P","D","m","P","G","R","G","R","'N","S","S","-","-"}}
                                                         }},
        {"Raga Desh - Uttaranga Dadra", "Desh", "3+3", 136, {
                                                                {"Form A (Sthayi)", 4, {"P","N","S'","R'","G'","R'","S'","n","D","P","m","P","R","m","P","n","D","P","m","G","R","S","-","-"}},
                                                                {"Form B (Antara)", 4, {"R","m","P","N","S'","S'","N","S'","R'","n","D","P","D","n","D","P","m","G","R","G","R","S","-","-"}}
                                                            }},
        {"Raga Puriya Dhanashri", "Puriya Dhanashri", "4+4", 128, {
                                                                      {"Form A (Sthayi)", 4, {"'N","r","G","M","P","-","d","P","M","G","M","r","G","r","S","-","'N","r","G","M","d","P","M","G","M","r","G","r","S","-","-","-"}},
                                                                      {"Form B (Antara)", 4, {"G","M","d","N","S'","-","S'","-","N","r'","G'","r'","S'","N","d","P","M","d","N","d","P","M","G","r","G","r","S","'N","S","-","-","-"}}
                                                                  }},
        {"Raga Bhairavi - Dhun 1", "Bhairavi", "4+4", 128, {
                                                               {"Form A (Sthayi)", 4, {"g","r","S","'n","S","-","r","g","m","-","P","d","P","m","g","r","g","m","P","m","g","r","S","'n","S","r","g","r","S","-","-","-"}},
                                                               {"Form B (Antara)", 4, {"m","P","d","n","S'","-","S'","-","n","S'","r'","g'","r'","S'","n","d","P","d","n","d","P","m","g","r","g","r","S","'n","S","-","-","-"}}
                                                           }},
        {"Raga Bhairavi - Dhun 2", "Bhairavi", "4+4", 130, {
                                                               {"Form A (Sthayi)", 4, {"S","r","g","m","P","-","P","-","m","P","d","P","m","g","r","S","r","g","m","P","m","g","r","'n","S","-","r","S","S","-","-","-"}},
                                                               {"Form B (Antara)", 4, {"m","P","d","n","S'","-","S'","-","n","S'","r'","S'","n","d","P","-","m","P","d","P","m","g","r","g","r","S","'n","S","S","-","-","-"}}
                                                           }},
        {"Raga Bhairava - Morning", "Bhairava", "4+4", 128, {
                                                                {"Form A (Sthayi)", 4, {"S","r","G","m","G","-","r","S","G","m","P","d","P","-","G","m","m","-","m","-","m","P","G","m","G","r","S","'N","S","-","-","-"}},
                                                                {"Form B (Antara)", 4, {"G","m","d","N","S'","-","S'","-","N","S'","r'","S'","N","d","P","-","G","m","P","d","P","m","G","r","G","r","S","'N","S","-","-","-"}}
                                                            }},
        {"Raga Ahir Bhairava Tintal", "Ahir Bhairava", "4+4+4+4", 145, {
                                                                           {"Form A (Sthayi)", 2, {"P","m","-","m","m","m","P","m","G","r","r","S","S","-","m","m","G","m","P","D","n","D","P","m","G","m","G","r","S","-","-","-"}},
                                                                           {"Form B (Antara)", 2, {"m","P","D","n","S'","-","S'","-","n","S'","r'","S'","n","D","P","m","G","m","P","D","n","D","P","m","G","r","'n","r","S","-","-","-"}}
                                                                       }},
        {"Raga Kaushikdhwani", "Kaushikdhwani", "4+4", 132, {
                                                                {"Form A (Sthayi)", 4, {"S'","S'","S'","-","S'","-","-","-","N","R'","S'","N","D","-","-","-","G","m","D","N","S'","N","D","m","G","m","G","S","S","-","-","-"}},
                                                                {"Form B (Antara)", 4, {"G","m","D","N","S'","-","S'","-","N","S'","G'","S'","N","D","m","-","G","m","D","N","D","m","G","S","'N","S","G","m","S","-","-","-"}}
                                                            }},
        {"Raga Bhupali - Pentatonic", "Bhupali", "4+4", 138, {
                                                                 {"Form A (Sthayi)", 4, {"S","R","G","-","R","G","P","G","D","P","G","R","G","R","S","'D","S","R","G","P","D","-","P","G","R","G","R","S","S","-","-","-"}},
                                                                 {"Form B (Antara)", 4, {"G","P","D","S'","S'","-","S'","-","D","S'","R'","G'","R'","S'","D","P","G","P","D","P","G","R","G","R","S","'D","S","-","S","-","-","-"}}
                                                             }},
        {"Raga Sivaranjani", "Sivaranjani", "4+4", 130, {
                                                            {"Form A (Sthayi)", 4, {"S","R","g","-","R","g","P","-","D","P","g","R","g","R","S","'D","S","R","g","P","D","P","g","R","g","R","S","-","S","-","-","-"}},
                                                            {"Form B (Antara)", 4, {"g","P","D","S'","S'","-","S'","-","D","S'","R'","g'","R'","S'","D","P","D","P","g","R","g","P","g","R","S","'D","S","-","S","-","-","-"}}
                                                        }},
        {"Raga Bageshri - Bandish", "Bageshri", "4+4", 128, {
                                                                {"Form A (Sthayi)", 4, {"'n","S","g","m","D","n","D","m","P","D","m","g","R","S","'n","S","S","g","m","D","n","D","m","g","m","g","R","S","S","-","-","-"}},
                                                                {"Form B (Antara)", 4, {"m","D","n","S'","S'","-","S'","-","n","S'","R'","g'","R'","S'","n","D","m","P","D","m","g","m","g","R","S","'n","S","-","S","-","-","-"}}
                                                            }},
        {"Raga Malkauns - Bandish", "Malkauns", "4+4", 124, {
                                                                {"Form A (Sthayi)", 4, {"'n","S","g","m","d","n","d","m","g","m","g","S","'n","'d","'n","S","S","g","m","d","m","g","m","g","S","'n","S","-","S","-","-","-"}},
                                                                {"Form B (Antara)", 4, {"g","m","d","n","S'","-","S'","-","n","S'","g'","S'","n","d","m","-","g","m","d","n","d","m","g","S","'n","S","g","S","S","-","-","-"}}
                                                            }},
        {"Raga Yaman - Bandish", "Yaman (Kalyana)", "4+4", 134, {
                                                                    {"Form A (Sthayi)", 4, {"'N","R","G","M","P","-","M","G","R","G","M","P","D","N","D","P","M","G","R","G","M","P","M","G","R","G","R","S","S","-","-","-"}},
                                                                    {"Form B (Antara)", 4, {"M","D","N","S'","S'","-","S'","-","N","R'","G'","R'","S'","N","D","P","M","D","N","D","P","M","G","R","G","R","S","'N","S","-","-","-"}}
                                                                }},
        {"Bilaval That - Alankar", "Bilaval That", "4+4", 140, {
                                                                   {"Form A (Aroha 4-Note)", 4, {"S","R","G","m","R","G","m","P","G","m","P","D","m","P","D","N","P","D","N","S'","D","N","S'","R'","N","S'","R'","G'","S'","-","-","-"}},
                                                                   {"Form B (Avaroha 4-Note)", 4, {"G'","R'","S'","N","R'","S'","N","D","S'","N","D","P","N","D","P","m","D","P","m","G","P","m","G","R","m","G","R","S","S","-","-","-"}}
                                                               }},
        {"Song with Synthesised Voice Template", "Template", "4+4", 120, {
                                                                     {"Part 1 (Sthayi)", 2,
                                                                         { // Swaras (Notes): S, R, G, m, P, D, N, etc. or '-' to hold the previous note
                                                                             "-", "-", "-", "-", "-", "-", "-", "-",
                                                                             "-", "-", "-", "-", "-", "-", "-", "-"
                                                                         },
                                                                         { // Lyrics: one syllable per cell, or '-' to sustain the previous vowel
                                                                             "-", "-", "-", "-", "-", "-", "-", "-",
                                                                             "-", "-", "-", "-", "-", "-", "-", "-"
                                                                         }
                                                                     }
                                                                 }, " ", {}}
    };
    return songs;
}

inline void parseSyllablePhoneme(const char* syl, float& outA1, float& outA2, float& outA3) {
    outA1 = 0.0f;
    outA2 = 0.15f;
    outA3 = 0.50f;

    if (!syl || !*syl || std::strcmp(syl, "-") == 0 || std::strcmp(syl, ".") == 0) {
        outA2 = 0.0f;
        return;
    }

    for (const char* p = syl; *p; ++p) {
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        if (c == 'a')      { outA1 = 0.00f; break; }
        else if (c == 'e') { outA1 = 0.25f; break; }
        else if (c == 'i' || c == 'y') { outA1 = 0.50f; break; }
        else if (c == 'o') { outA1 = 0.75f; break; }
        else if (c == 'u') { outA1 = 1.00f; break; }
    }

    char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(syl[0])));
    if (c0 == 's' || c0 == 'h') {
        outA2 = 0.85f;
        outA3 = 0.80f;
    } else if (c0 == 'k' || c0 == 't' || c0 == 'p' || c0 == 'c') {
        outA2 = 0.65f;
        outA3 = 0.60f;
    } else if (c0 == 'd' || c0 == 'b' || c0 == 'g' || c0 == 'j' || c0 == 'v') {
        outA2 = 0.35f;
        outA3 = 0.45f;
    } else if (c0 == 'n' || c0 == 'm') {
        outA2 = 0.10f;
        outA3 = 0.30f;
    } else if (c0 == 'r' || c0 == 'l') {
        outA2 = 0.18f;
        outA3 = 0.40f;
    }
}

inline int parseTotalMatras(const char* divStr) {
    if (!divStr) return 8;
    int total = 0;
    const char* p = divStr;
    while (*p) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        int v = std::strtol(p, const_cast<char**>(&p), 10);
        if (v > 0 && v <= 16) total += v;
    }
    return total > 0 ? total : 8;
}

inline bool parseSwaraToken(const char* tok, int& outMidiKey) {
    if (!tok) return false;
    while (*tok == ' ') tok++;
    if (!*tok || *tok == '-' || *tok == '.') return false;

    int octaveShift = 0;
    while (*tok == '\'') { octaveShift -= 12; tok++; }
    char c = *tok;
    if (!c) return false;
    const char* end = tok + 1;
    while (*end == '\'') { octaveShift += 12; end++; }

    int semi = -1;
    if (c == 'S' || c == 's') semi = 0;
    else if (c == 'r') semi = 1;
    else if (c == 'R') semi = 2;
    else if (c == 'g') semi = 3;
    else if (c == 'G') semi = 4;
    else if (c == 'm') semi = 5;
    else if (c == 'M') semi = 6;
    else if (c == 'P' || c == 'p') semi = 7;
    else if (c == 'd') semi = 8;
    else if (c == 'D') semi = 9;
    else if (c == 'n') semi = 10;
    else if (c == 'N') semi = 11;

    if (semi < 0) return false;
    outMidiKey = 60 + octaveShift + semi;
    return true;
}

inline bool strEqualsCI(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
    return *a == *b;
}

inline int findTrackIndexCI(const char* name) {
    const auto& trks = getTracks();
    for (size_t i = 0; i < trks.size(); ++i) {
        if (strEqualsCI(trks[i].name, name)) return static_cast<int>(i);
    }
    return -1;
}

} // namespace BuiltInPresets

#endif // DRUM_PRESETS_H
