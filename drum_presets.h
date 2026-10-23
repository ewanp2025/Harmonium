#ifndef DRUM_PRESETS_H
#define DRUM_PRESETS_H

#include "projectio.h"

namespace BuiltInPresets {

inline QString defaultHarmoniumO1() {
    return "(1 - exp(-t * 8)) * exp(-trel * 18) * (0.82 + 0.18 * sinew(integrate(tempo / 120))) * ( "
           "(sinew(integrate(f * cent(-4 * sinew(integrate(tempo / 120))))) - sinew(2 * integrate(f))/2 + "
           "(0.75 + 0.25 * sinew(integrate(tempo / 120))) * (sinew(3 * integrate(f))/3 - sinew(4 * integrate(f))/4 + sinew(5 * integrate(f))/5)) + "
           "0.45 * (sinew(integrate(f * cent(8))) - sinew(2 * integrate(f * cent(8)))/2 + sinew(3 * integrate(f * cent(8)))/3) + "
           "0.65 * (sinew(integrate(f / 2)) - sinew(2 * integrate(f / 2))/2 + sinew(3 * integrate(f / 2))/3) )";
}

inline QString shrutiDroneO1() {
    return "0.3 * (0.55*saww(integrate(f*cent(-5))) + 0.35*squarew(integrate(f*cent(4))) + 0.2*sinew(integrate(f*2))) * "
           "(0.92 + 0.08*sinew(t*tempo/60)) * min(1, t*15) * ((1 - rel) + rel*exp(-trel*6))";
}
inline QString shrutiDroneO2() {
    return "0.3 * (0.55*saww(integrate(f*cent(5))) + 0.35*squarew(integrate(f*cent(-4))) + 0.2*sinew(integrate(f*2))) * "
           "(0.92 + 0.08*sinew(t*tempo/60)) * min(1, t*15) * ((1 - rel) + rel*exp(-trel*6))";
}

inline QString tanpuraDroneO1() {
    return "0.35 * (sinew(integrate(f) + (0.6 + 0.35*sinew(t*tempo/240))*sinew(integrate(f*2)) + 0.25*sinew(integrate(f*3))) + "
           "0.4*saww(integrate(f*cent(-3)))*exp(-mod(t*tempo/120, 1)*2.2)) * min(1, t*20) * ((1 - rel) + rel*exp(-trel*3))";
}
inline QString tanpuraDroneO2() {
    return "0.35 * (sinew(integrate(f*cent(3)) + (0.6 + 0.35*cos(t*tempo/240))*sinew(integrate(f*2)) + 0.25*sinew(integrate(f*3))) + "
           "0.4*saww(integrate(f))*exp(-mod(t*tempo/120 + 0.5, 1)*2.2)) * min(1, t*20) * ((1 - rel) + rel*exp(-trel*3))";
}

inline MmpSong createDefaultTalaBank() {
    MmpSong s;
    s.title = "Preset Bank";
    s.bpm = 140;
    s.saSemitone = 0; // C4 = Sa
    s.a4Hz = 440.0;

    auto addInst = [&](const QString& name, bool isBeat, const QString& o1, const QString& o2,
                       bool useAdsr = false, double dec = 0.5, double sus = 0.5, double rel = 0.1) {
        MmpTrack t;
        t.name = name;
        t.isBeatTrack = isBeat;
        t.muted = false;
        t.vol = 1.0;
        t.baseNote = 69;
        t.A1 = 1.0; t.A2 = 1.0; t.A3 = 1.0;
        t.o1Str = o1;
        t.o2Str = o2;
        t.useAdsr = useAdsr;
        t.dec = dec;
        t.sus = sus;
        t.rel = rel;
        ProjectIO::compileTrackAsts(t);
        s.tracks.push_back(std::move(t));
    };

    // --- DRUM / PERCUSSION INSTRUMENTS (Indices 0..13) ---
    addInst("Dha", true,
            "0.65 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.2 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.2 * exp(-10 * t) * sin(2 * pi * f * 1.1892 * t) + 0.15 * exp(-75 * t) * sin(2 * pi * 1800 * t)",
            "0.35 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.45 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.3 * exp(-14 * t) * sin(2 * pi * f * 1.1892 * t) + 0.25 * exp(-75 * t) * sin(2 * pi * 1800 * t)");

    addInst("Dhin", true,
            "0.65 * exp(-4.5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.5 * exp(-20 * t))) + 0.25 * exp(-7 * t) * sin(2 * pi * f * 0.5946 * t) + 0.15 * exp(-9 * t) * sin(2 * pi * f * 1.1892 * t)",
            "0.35 * exp(-4.5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.5 * exp(-20 * t))) + 0.5 * exp(-6.5 * t) * sin(2 * pi * (f * 0.5946 * t - 0.08 * exp(-35 * t))) + 0.35 * exp(-9 * t) * sin(2 * pi * f * 1.1892 * t) + 0.2 * exp(-14 * t) * sin(2 * pi * f * 1.7838 * t)");

    addInst("Ge", true,
            "0.75 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.25 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.2 * exp(-60 * t) * sin(2 * pi * 280 * t)",
            "0.5 * exp(-5 * t) * sin(2 * pi * (f * 0.2973 * t - 0.6 * exp(-25 * t))) + 0.15 * exp(-14 * t) * sin(2 * pi * f * 0.5946 * t) + 0.12 * exp(-60 * t) * sin(2 * pi * 280 * t)");

    addInst("Ka", true,
            "0.6 * exp(-35 * t) * sin(2 * pi * (160 * t - 0.4 * exp(-50 * t))) + 0.35 * exp(-90 * t) * sin(2 * pi * 420 * t) + 0.25 * exp(-140 * t) * sin(2 * pi * 950 * t)",
            "0.4 * exp(-35 * t) * sin(2 * pi * (160 * t - 0.4 * exp(-50 * t))) + 0.22 * exp(-90 * t) * sin(2 * pi * 420 * t) + 0.15 * exp(-140 * t) * sin(2 * pi * 950 * t)");

    addInst("Na", true,
            "0.35 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.3 * exp(-12 * t) * sin(2 * pi * f * 1.1892 * t) + 0.15 * exp(-22 * t) * sin(2 * pi * f * 1.7838 * t) + 0.18 * exp(-85 * t) * sin(2 * pi * f * 3.5 * t)",
            "0.55 * exp(-10 * t) * sin(2 * pi * (f * 0.5946 * t - 0.12 * exp(-40 * t))) + 0.45 * exp(-12 * t) * sin(2 * pi * f * 1.1892 * t) + 0.22 * exp(-22 * t) * sin(2 * pi * f * 1.7838 * t) + 0.28 * exp(-85 * t) * sin(2 * pi * f * 3.5 * t)");

    addInst("Ta", true,
            "0.4 * exp(-14 * t) * sin(2 * pi * (f * 0.5946 * t - 0.18 * exp(-45 * t))) + 0.2 * exp(-20 * t) * sin(2 * pi * f * 1.1892 * t) + 0.25 * exp(-90 * t) * sin(2 * pi * 1800 * t)",
            "0.65 * exp(-14 * t) * sin(2 * pi * (f * 0.5946 * t - 0.18 * exp(-45 * t))) + 0.3 * exp(-20 * t) * sin(2 * pi * f * 1.1892 * t) + 0.38 * exp(-90 * t) * sin(2 * pi * 1800 * t)");

    addInst("Ti", true,
            "0.32 * exp(-85 * t) * sin(2 * pi * 820 * t) + 0.25 * exp(-130 * t) * sin(2 * pi * 1350 * t) + 0.18 * exp(-220 * t) * sin(2 * pi * 2500 * t)",
            "0.5 * exp(-85 * t) * sin(2 * pi * 820 * t) + 0.4 * exp(-130 * t) * sin(2 * pi * 1350 * t) + 0.3 * exp(-220 * t) * sin(2 * pi * 2500 * t)");

    addInst("Re", true,
            "0.30 * exp(-65 * t) * sin(2 * pi * 900 * t) + 0.22 * exp(-110 * t) * sin(2 * pi * 1480 * t) + 0.16 * exp(-180 * t) * sin(2 * pi * 2750 * t)",
            "0.48 * exp(-65 * t) * sin(2 * pi * 900 * t) + 0.38 * exp(-110 * t) * sin(2 * pi * 1480 * t) + 0.26 * exp(-180 * t) * sin(2 * pi * 2750 * t)");

    addInst("Kartal Open", true,
            "0.45 * exp(-3 * t) * sin(2 * pi * 3120 * t + 1.5 * exp(-8 * t) * sin(2 * pi * 4412 * t)) + 0.35 * exp(-4.5 * t) * sin(2 * pi * 4850 * t + 1.2 * exp(-12 * t) * sin(2 * pi * 6858 * t)) + 0.3 * exp(-70 * t) * sin(2 * pi * 7200 * t + 4 * sin(2 * pi * 5100 * t))",
            "0.45 * exp(-2.8 * t) * sin(2 * pi * 3126 * t + 1.5 * exp(-8 * t) * sin(2 * pi * 4420 * t)) + 0.35 * exp(-4.2 * t) * sin(2 * pi * 4858 * t + 1.2 * exp(-12 * t) * sin(2 * pi * 6866 * t)) + 0.3 * exp(-70 * t) * sin(2 * pi * 7215 * t + 4 * sin(2 * pi * 5110 * t))");

    addInst("Kartal Closed", true,
            "0.5 * exp(-40 * t) * sin(2 * pi * 3120 * t + 3 * exp(-60 * t) * sin(2 * pi * 4412 * t)) + 0.4 * exp(-55 * t) * sin(2 * pi * 4850 * t + 3.5 * exp(-70 * t) * sin(2 * pi * 6858 * t)) + 0.4 * exp(-100 * t) * sin(2 * pi * 7500 * t + 6 * sin(2 * pi * 5300 * t))",
            "0.5 * exp(-40 * t) * sin(2 * pi * 3126 * t + 3 * exp(-60 * t) * sin(2 * pi * 4420 * t)) + 0.4 * exp(-55 * t) * sin(2 * pi * 4858 * t + 3.5 * exp(-70 * t) * sin(2 * pi * 6866 * t)) + 0.4 * exp(-100 * t) * sin(2 * pi * 7515 * t + 6 * sin(2 * pi * 5310 * t))");

    addInst("Ghanta", true,
            "clamp(-1, 0.35 * ( 0.45 * exp(-0.35 * t) * (sin(6.28318 * 0.5 * f * t) + 0.8 * sin(6.28318 * (0.5 * f + 0.8 + A2 * 1.5) * t)) + 0.90 * exp(-1.1 * t) * (sin(6.28318 * f * t + 1.8 * exp(-35 * t) * sin(6.28318 * 2.756 * f * t)) + 0.6 * sin(6.28318 * (f * 1.0035) * t)) + 0.55 * exp(-1.9 * t) * sin(6.28318 * 1.183 * f * t) + 0.70 * exp(-2.4 * t) * (sin(6.28318 * 2.0 * f * t) + 0.5 * sin(6.28318 * (2.0025 * f) * t)) + 0.45 * exp(-4.5 * t) * sin(6.28318 * 2.756 * f * t) + 0.30 * exp(-8.0 * t) * sin(6.28318 * 4.08 * f * t) + 0.20 * exp(-14.0 * t) * sin(6.28318 * 5.41 * f * t) + 0.35 * exp(-80 * t) * randv(t * srate) * (0.4 + 0.6 * sin(6.28318 * 3200 * t)) ), 1)", "");

    addInst("Kansa", true,
            "clamp(-1, 0.38 * ( 0.85 * exp(-0.9 * t) * sin(6.28318 * f * (1 + 0.025 * exp(-12 * t)) * t + 0.8 * exp(-2.5 * t) * sin(6.28318 * 1.618 * f * t)) + 0.65 * exp(-1.4 * t) * sin(6.28318 * 1.414 * f * t + (2.5 + A1 * 2.0) * (1 - exp(-8 * t)) * exp(-1.8 * t) * sin(6.28318 * 2.414 * f * t)) + 0.50 * (1 - exp(-6 * t)) * exp(-1.6 * t) * sin(6.28318 * 2.236 * f * t + (3.5 + A2 * 3.0) * (1 - exp(-5 * t)) * exp(-2.2 * t) * sin(6.28318 * 3.618 * f * t)) + 0.35 * (1 - exp(-4 * t)) * exp(-2.5 * t) * sin(6.28318 * 3.162 * f * t) + (0.18 * (1 - exp(-5 * t)) * exp(-2.0 * t) + 0.25 * exp(-60 * t)) * randv(t * srate) * sin(6.28318 * f * 4.5 * t) ) + 0.22 * last(1), 1)", "");

    addInst("Manjira", true,
            "clamp(-1, 0.40 * ( 0.70 * exp(-3.5 * t) * (sin(6.28318 * (f * 4.0) * t) + 0.8 * sin(6.28318 * (f * 4.015) * t)) + 0.55 * exp(-5.0 * t) * sin(6.28318 * (f * 5.38) * t + 2.2 * exp(-15 * t) * sin(6.28318 * (f * 7.12) * t)) + 0.40 * exp(-8.0 * t) * sin(6.28318 * (f * 6.85) * t) + 0.30 * exp(-18.0 * t) * sin(6.28318 * (f * 9.20) * t) + (0.45 * exp(-120 * t) + 0.12 * exp(-12 * t)) * randv(t * srate) * (0.3 + 0.7 * sin(6.28318 * 6500 * t)) ), 1)", "");

    addInst("Damaru", true,
            "clamp(-1, last(1) + clamp(0.01, 6.28318 * (f * 3.5 + 800 * exp(-35 * t) + A1 * 1000) / srate, 0.98) * ( ( 0.90 * exp(-7.5 * t) * sinew(integrate(f * (1 + (0.45 + A2 * 0.4) * exp(-28 * t)))) + 0.45 * exp(-14.0 * t) * sinew(integrate(1.593 * f * (1 + 0.30 * exp(-35 * t)))) + 0.25 * exp(-22.0 * t) * sinew(integrate(2.135 * f * (1 + 0.20 * exp(-40 * t)))) + (0.55 * exp(-90 * t) + 0.35 * exp(-70 * mod(t, 0.125 + A3 * 0.08)) * (t < 0.38)) * randv(t * srate) * (0.4 + 0.6 * sin(6.28318 * 1400 * t)) ) - last(1) ), 1)", "");

    // MELODIC / PRESET INSTRUMENTS (Indices 14..16) ---
    addInst("Shankha", false,
            "clamp(-1, last(1) + clamp(0.01, 6.28318 * f * (2.2 + A1 * 2.0) / srate, 0.92) * ( 0.28 * ( (1 - exp(-10 * t)) * exp(-0.16 * t) * ( sin(6.28318 * f * (1 - 0.18 * exp(-15 * t)) * t) + 0.65 * sin(12.56636 * f * (1 - 0.18 * exp(-15 * t)) * t) + 0.40 * (1 - exp(-4 * t)) * sin(18.84955 * f * (1 - 0.18 * exp(-15 * t)) * t) ) + ((t > 0.06) * (1 - exp(-8 * (t - 0.06))) * exp(-0.15 * (t - 0.06)) * ( 0.85 * sin(6.28318 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) + 0.75 * sin(12.56636 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) + 0.55 * (1 - exp(-3.5 * (t - 0.06))) * sin(18.84955 * (f * 1.006 + A2 * 3.0) * (1 - 0.15 * exp(-12 * (t - 0.06))) * (t - 0.06)) )) + (0.10 * exp(-5 * t) + 0.025) * randv(t * srate) ) - last(1) ), 1)", "");

    addInst("Shankha 2", false,
            "clamp(-1, last(1) + clamp(0.02, 6.28318 * (1200 + f * 1.5 + A1 * 800) / srate, 0.96) * ( (1 - exp(-18 * t)) * exp(-0.08 * t) * (0.88 + 0.12 * sinew(5.5 * t)) * ( 0.45 * saww(integrate(f * (1 - 0.08 * exp(-25 * t)) * (1 + (0.018 + A2 * 0.04) * sinew(5.8 * t)))) + 0.35 * squarew(integrate(f * 1.001 * (1 - 0.08 * exp(-25 * t)) * (1 + (0.018 + A2 * 0.04) * sinew(5.8 * t)))) + 0.30 * sin(6.28318 * 2.0 * f * (1 + (0.018 + A2 * 0.04) * sin(6.28318 * 5.8 * t)) * t) + 0.25 * sin(6.28318 * 3.0 * f * (1 + (0.018 + A2 * 0.04) * sin(6.28318 * 5.8 * t)) * t) + (0.09 * exp(-15 * t) + 0.04) * randv(t * srate) * sin(6.28318 * 1800 * t) ) - last(1) ), 1)", "");

    addInst("Harmonium_Bellows", false, defaultHarmoniumO1(), "", true, 2.0, 1.0, 0.348);


    // Tala Definitions (Vibhag Divisions + Matra Bol/Kartal Cells)

    s.talaThekas = {
        {
            "Kaherva (8 Matra)", "4+4",
            {"Dha", "Ge", "Na", "Ti", "Na", "Ka", "Dhin", "Na"},
            {"Kartal Open", "-", "Kartal Closed", "Kartal Closed", "Kartal Open", "-", "Kartal Closed", "Kartal Closed"}
        },
        {
            "Bhajani (8 Matra)", "4+4",
            {"Dhin", "Ta Dhin", "Dhin", "Ta", "Ti", "Ta Ti", "Ti", "Ta"},
            {"Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Open", "Kartal Closed"}
        },
        {
            "Tintal (16 Matra)", "4+4+4+4",
            {"Dha", "Dhin", "Dhin", "Dha", "Dha", "Dhin", "Dhin", "Dha", "Dha", "Ti", "Ti", "Ta", "Ta", "Dhin", "Dhin", "Dha"},
            {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Closed", "-", "-", "-", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed"}
        },
        {
            "Dadra (6 Matra)", "3+3",
            {"Dha", "Dhin", "Na", "Dha", "Ti", "Na"},
            {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed"}
        }
    };


    // Harmonium Songs (Forms + Vibhag Divisions + Swara Cells)

    s.harmoniumSongs = {
        {
            "TEST1", "NOTE1", "4+4+4+4", 135,
            {
                {
                    "Form A (Sthayi)", 2,
                    {
                        "S",  "-",  "R",  "g",   "R",  "-",  "S",  "'n",  "S",  "-",  "R",  "m",   "P",  "-",  "d",  "P",
                        "m",  "P",  "d",  "n",   "d",  "P",  "m",  "g",   "R",  "g",  "R",  "S",   "'n", "S",  "-",  "-"
                    }
                },
                {
                    "Form B (Antara)", 2,
                    {
                        "m",  "P",  "d",  "n",   "S'", "-",  "S'", "-",   "n",  "S'", "R'", "S'",  "n",  "d",  "P",  "-",
                        "m",  "P",  "d",  "P",   "m",  "g",  "R",  "g",   "R",  "-",  "S",  "'n",  "S",  "-",  "-",  "-"
                    }
                }
            }
        },
        {
            "TEST2", "NOTE2", "4+4", 130,
            {
                {
                    "Form A (Sthayi)", 4,
                    {
                        "S",  "-",  "-",  "R",   "g",  "-",  "m",  "g",
                        "R",  "-",  "S",  "'n",  "S",  "-",  "-",  "P",
                        "P",  "-",  "-",  "m",   "g",  "R",  "R",  "g",
                        "m",  "-",  "g",  "R",   "S",  "-",  "-",  "'P"
                    }
                },
                {
                    "Form B (Antara)", 4,
                    {
                        "S",  "-",  "R",  "-",   "g",  "-",  "m",  "g",
                        "R",  "-",  "S",  "'n",  "S",  "-",  "-",  "S",
                        "P",  "P",  "P",  "m",   "g",  "R",  "R",  "g",
                        "m",  "-",  "g",  "R",   "S",  "-",  "-",  "-"
                    }
                }
            }
        }
    };

    ProjectIO::rebuildCompiledPatterns(s);
    return s;
}

} // namespace BuiltInPresets

#endif // DRUM_PRESETS_H
