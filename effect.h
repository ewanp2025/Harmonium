#ifndef EFFECT_H
#define EFFECT_H

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

struct BiquadFilter {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }
    void configureLowpass(double freq, double q, double sr);
    void configureHighpass(double freq, double q, double sr);
    void configurePeak(double freq, double q, double gainDb, double sr);
    void configureLowShelf(double freq, double q, double gainDb, double sr);
    void configureHighShelf(double freq, double q, double gainDb, double sr);

    inline double process(double in) {
        double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }
};

// 8-Band Equalizer
class EightBandEQ {
public:
    explicit EightBandEQ(double sampleRate = 44100.0);
    void setSampleRate(double sr);
    void updateBands(double peak320Db, double peak1k3Db, double lpfCutoffHz);
    double process(double input);

    bool enabled = true;
    double inGain = 1.0;
    double outGain = 1.0;
    double m_peak320Db = 4.5;
    double m_peak1k3Db = 2.5;
    double m_lpfCutoff = 7200.0;

private:
    double m_sr = 44100.0;
    BiquadFilter m_bands[5]; // Active bands: LowShelf, Peak3, Peak4, HighShelf7, LPF8
};

// Plate2x2 Reverb (bandwidth, tail, damping, blend)
class Plate2x2Reverb {
public:
    explicit Plate2x2Reverb(double sampleRate = 44100.0);
    void setParameters(double bandwidth, double tail, double damping, double blend);
    double process(double input);

    bool enabled = true;
    double bandwidth = 0.60;
    double tail = 0.48;
    double damping = 0.32;
    double blend = 0.25;

private:
    struct AllPass {
        std::vector<double> buf;
        size_t idx = 0;
        double g = 0.5;
        void init(size_t len, double gain) { buf.assign(std::max<size_t>(1, len), 0.0); idx = 0; g = gain; }
        inline double process(double x) {
            double bu = buf[idx];
            double y = -x * g + bu;
            buf[idx] = x + y * g;
            if (++idx >= buf.size()) idx = 0;
            return y;
        }
    };
    struct Comb {
        std::vector<double> buf;
        size_t idx = 0;
        double store = 0.0;
        void init(size_t len) { buf.assign(std::max<size_t>(1, len), 0.0); idx = 0; store = 0.0; }
        inline double process(double x, double fb, double damp) {
            double y = buf[idx];
            store = y * (1.0 - damp) + store * damp;
            buf[idx] = x + store * fb;
            if (++idx >= buf.size()) idx = 0;
            return y;
        }
    };

    double m_bwState = 0.0;
    AllPass m_inDiff[4];
    Comb m_tankCombs[4];
    AllPass m_tankAp[2];
};

#endif // EFFECT_H
