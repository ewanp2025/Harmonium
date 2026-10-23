#include "effect.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clampFreq(double freq, double sr) {
    return std::clamp(freq, 20.0, std::max(100.0, sr * 0.45));
}

void BiquadFilter::configureLowpass(double freq, double q, double sr) {
    freq = clampFreq(freq, sr);
    q = std::max(0.1, q);
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha;
    b0 = ((1.0 - cosw) * 0.5) / a0;
    b1 = (1.0 - cosw) / a0;
    b2 = b0;
    a1 = (-2.0 * cosw) / a0;
    a2 = (1.0 - alpha) / a0;
}

void BiquadFilter::configureHighpass(double freq, double q, double sr) {
    freq = clampFreq(freq, sr);
    q = std::max(0.1, q);
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha;
    b0 = ((1.0 + cosw) * 0.5) / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = (-2.0 * cosw) / a0;
    a2 = (1.0 - alpha) / a0;
}

void BiquadFilter::configurePeak(double freq, double q, double gainDb, double sr) {
    freq = clampFreq(freq, sr);
    q = std::max(0.1, q);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);
    double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = (-2.0 * cosw) / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = b1;
    a2 = (1.0 - alpha / A) / a0;
}

void BiquadFilter::configureLowShelf(double freq, double q, double gainDb, double sr) {
    freq = clampFreq(freq, sr);
    q = std::max(0.1, q);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);
    double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
    double a0 = (A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha;
    b0 = (A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha)) / a0;
    b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosw)) / a0;
    b2 = (A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha)) / a0;
    a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosw)) / a0;
    a2 = ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha) / a0;
}

void BiquadFilter::configureHighShelf(double freq, double q, double gainDb, double sr) {
    freq = clampFreq(freq, sr);
    q = std::max(0.1, q);
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);
    double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
    double a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha;
    b0 = (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha)) / a0;
    b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw)) / a0;
    b2 = (A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha)) / a0;
    a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw)) / a0;
    a2 = ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha) / a0;
}

EightBandEQ::EightBandEQ(double sampleRate) {
    setSampleRate(sampleRate);
}

void EightBandEQ::setSampleRate(double sr) {
    m_sr = std::max(8000.0, sr);
    for (auto& b : m_bands) b.reset();
    updateBands(m_peak320Db, m_peak1k3Db, m_lpfCutoff);
}

void EightBandEQ::updateBands(double peak320Db, double peak1k3Db, double lpfCutoffHz) {
    m_peak320Db = peak320Db;
    m_peak1k3Db = peak1k3Db;
    m_lpfCutoff = lpfCutoffHz;

    // Warm Harmonium & Mridanga EQ curve
    m_bands[0].configureLowShelf(95.0, 0.707, 1.5, m_sr);
    m_bands[1].configurePeak(320.0, 1.1, m_peak320Db, m_sr);
    m_bands[2].configurePeak(1300.0, 1.2, m_peak1k3Db, m_sr);
    m_bands[3].configureHighShelf(4200.0, 0.707, -1.5, m_sr);
    m_bands[4].configureLowpass(m_lpfCutoff, 0.707, m_sr);
}

double EightBandEQ::process(double input) {
    if (!enabled) return input;
    double x = input * inGain;
    for (int i = 0; i < 5; ++i) {
        x = m_bands[i].process(x);
    }
    return x * outGain;
}

Plate2x2Reverb::Plate2x2Reverb(double sampleRate) {
    double scale = std::max(0.25, sampleRate / 44100.0);
    m_inDiff[0].init(static_cast<size_t>(142 * scale), 0.75);
    m_inDiff[1].init(static_cast<size_t>(107 * scale), 0.75);
    m_inDiff[2].init(static_cast<size_t>(379 * scale), 0.625);
    m_inDiff[3].init(static_cast<size_t>(277 * scale), 0.625);

    m_tankCombs[0].init(static_cast<size_t>(1116 * scale));
    m_tankCombs[1].init(static_cast<size_t>(1356 * scale));
    m_tankCombs[2].init(static_cast<size_t>(1422 * scale));
    m_tankCombs[3].init(static_cast<size_t>(1617 * scale));

    m_tankAp[0].init(static_cast<size_t>(556 * scale), 0.5);
    m_tankAp[1].init(static_cast<size_t>(441 * scale), 0.5);
}

void Plate2x2Reverb::setParameters(double bw, double t, double damp, double bl) {
    bandwidth = std::clamp(bw, 0.05, 0.99);
    tail = std::clamp(t, 0.0, 0.95);
    damping = std::clamp(damp, 0.0, 0.95);
    blend = std::clamp(bl, 0.0, 1.0);
}

double Plate2x2Reverb::process(double input) {
    if (!enabled || blend <= 0.001) return input;

    // Input bandwidth filter
    m_bwState = input * bandwidth + m_bwState * (1.0 - bandwidth);
    double d = m_bwState;
    for (int i = 0; i < 4; ++i) d = m_inDiff[i].process(d);

    double fb = 0.70 + 0.26 * tail;
    double tank = 0.0;
    for (int i = 0; i < 4; ++i) {
        tank += m_tankCombs[i].process(d, fb, damping);
    }
    tank *= 0.25;
    tank = m_tankAp[0].process(tank);
    tank = m_tankAp[1].process(tank);

    return input * (1.0 - blend) + tank * blend;
}
