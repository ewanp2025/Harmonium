#ifndef EFFECT_H
#define EFFECT_H

#include <vector>
#include <complex>
#include <QString>
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

class TransferFunctionFX {
public:
    explicit TransferFunctionFX(double sampleRate = 22050.0);
    void setSampleRate(double sr);
    void setFormula(const QString& formula);
    float process(float input);

    bool enabled = false;

private:
    double m_sr = 22050.0;
    QString m_formula;

    int m_fftSize = 512;
    int m_hopSize = 128;
    int m_hopPos = 0;
    int m_silentSamples = 10000;

    std::vector<int> m_bitRev;
    std::vector<std::complex<float>> m_twiddlesFwd;
    std::vector<std::complex<float>> m_twiddlesInv;
    std::vector<std::complex<float>> m_filterCurve;
    std::vector<std::complex<float>> m_fftBuffer;

    std::vector<float> m_window;
    std::vector<float> m_history;
    std::vector<float> m_overlapAdd;
    std::vector<float> m_inputQueue;
    std::vector<float> m_outputQueue;
};

#endif // EFFECT_H