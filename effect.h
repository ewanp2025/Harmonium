#ifndef EFFECT_H
#define EFFECT_H

#include <complex>
#include <string>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

namespace {
using Complex = std::complex<float>;

// Fast inline complex multiply without Annex-G NaN/Inf branch overhead
inline Complex fastComplexMul(const Complex& a, const Complex& b) {
    return Complex(a.real() * b.real() - a.imag() * b.imag(),
                   a.real() * b.imag() + a.imag() * b.real());
}

class SimpleParser {
    const char* str = nullptr;
    size_t len = 0;
    size_t pos = 0;
    float currentFreq = 1.0f;

    char peek() {
        while (pos < len && std::isspace(static_cast<unsigned char>(str[pos]))) pos++;
        if (pos >= len) return 0;
        return str[pos];
    }

    char get() {
        char c = peek();
        if (pos < len) pos++;
        return c;
    }

    Complex parsePrimary() {
        char c = peek();
        if (c == '(') {
            get();
            Complex val = parseExpression();
            if (peek() == ')') get();
            return val;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const char* startPtr = str + pos;
            char* endPtr = nullptr;
            float val = std::strtof(startPtr, &endPtr);
            pos = static_cast<size_t>(endPtr - str);
            if (peek() == 'j') { get(); return Complex(0.0f, val); }
            return Complex(val, 0.0f);
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            char token[16];
            int tLen = 0;
            while (pos < len && std::isalnum(static_cast<unsigned char>(str[pos]))) {
                if (tLen < 15) token[tLen++] = str[pos];
                pos++;
            }
            token[tLen] = '\0';

            if (std::strcmp(token, "freq") == 0) return Complex(currentFreq, 0.0f);
            if (std::strcmp(token, "w") == 0)    return Complex(2.0f * M_PI_F * currentFreq, 0.0f);
            if (std::strcmp(token, "s") == 0)    return Complex(0.0f, 2.0f * M_PI_F * currentFreq);
            if (std::strcmp(token, "j") == 0)    return Complex(0.0f, 1.0f);
            if (std::strcmp(token, "pi") == 0)   return Complex(M_PI_F, 0.0f);

            if (peek() == '(') {
                get();
                Complex arg = parseExpression();
                if (peek() == ')') get();

                if (std::strcmp(token, "sqrt") == 0)  return std::sqrt(arg);
                if (std::strcmp(token, "exp") == 0)   return std::exp(arg);
                if (std::strcmp(token, "sin") == 0)   return std::sin(arg);
                if (std::strcmp(token, "cos") == 0)   return std::cos(arg);
                if (std::strcmp(token, "tan") == 0)   return std::tan(arg);
                if (std::strcmp(token, "sinh") == 0)  return std::sinh(arg);
                if (std::strcmp(token, "cosh") == 0)  return std::cosh(arg);
                if (std::strcmp(token, "tanh") == 0)  return std::tanh(arg);
                if (std::strcmp(token, "abs") == 0)   return Complex(std::abs(arg), 0.0f);
                if (std::strcmp(token, "log") == 0)   return std::log(arg);
                if (std::strcmp(token, "log10") == 0) return std::log10(arg);
            }
            return Complex(1.0f, 0.0f);
        }
        return Complex(0.0f, 0.0f);
    }

    Complex parsePower() {
        Complex lhs = parsePrimary();
        while (peek() == '^') {
            get();
            Complex rhs = parseUnary();
            lhs = std::pow(lhs, rhs);
        }
        return lhs;
    }

    Complex parseUnary() {
        char c = peek();
        if (c == '-') { get(); return -parseUnary(); }
        if (c == '+') { get(); return parseUnary(); }
        return parsePower();
    }

    Complex parseTerm() {
        Complex lhs = parseUnary();
        while (true) {
            char op = peek();
            if (op == '*') { get(); lhs = fastComplexMul(lhs, parseUnary()); }
            else if (op == '/') {
                get();
                Complex rhs = parseUnary();
                if (std::abs(rhs) < 1e-9f) rhs = Complex(1e-9f, 0.0f);
                lhs /= rhs;
            }
            else break;
        }
        return lhs;
    }

    Complex parseExpression() {
        Complex lhs = parseTerm();
        while (true) {
            char op = peek();
            if (op == '+') { get(); lhs += parseTerm(); }
            else if (op == '-') { get(); lhs -= parseTerm(); }
            else break;
        }
        return lhs;
    }

public:
    void setExpression(const std::string& expression) {
        str = expression.c_str();
        len = expression.length();
    }

    Complex eval(float f) {
        if (!str || len == 0) return Complex(1.0f, 0.0f);
        pos = 0;
        currentFreq = f;
        return parseExpression();
    }
};
} // namespace

class TransferFunctionFX {
public:
    static constexpr int FFT_SIZE  = 512;
    static constexpr int HOP_SIZE  = 128;
    static constexpr int HALF_FFT  = FFT_SIZE / 2;

    static const char* defaultCombinedHarmoniumTf() {
        return "((1 + 0.4 * exp(-(((freq-95)/55)^2)) + 2.5 * exp(-(((freq-320)/100)^2)) + 1.2 * exp(-(((freq-1300)/200)^2))) / sqrt(1 + (freq/7200)^8)) * "
               "(1 + 0.35 * exp(-j * 2 * pi * freq * 0.0020) - 0.22 * exp(-j * 2 * pi * freq * 0.0067) + 0.16 * exp(-j * 2 * pi * freq * 0.0131))";
    }

    explicit TransferFunctionFX(float sampleRate = 22050.0f) {
        initTables();
        setSampleRate(sampleRate);
        setFormula(defaultCombinedHarmoniumTf());
    }

    void setSampleRate(float sr) {
        m_sr = std::max(8000.0f, sr);

        // 1. Configure Fast Biquad + Comb Path
        m_bp95.setBandpass(95.0f, 90.0f, m_sr);
        m_bp320.setBandpass(320.0f, 165.0f, m_sr);
        m_bp1300.setBandpass(1300.0f, 330.0f, m_sr);
        m_lp7200.setLowpass(std::min(7200.0f, m_sr * 0.42f), 0.7071f, m_sr);

        m_d1 = std::clamp(static_cast<int>(0.0020f * m_sr + 0.5f), 1, COMB_BUF_LEN - 1);
        m_d2 = std::clamp(static_cast<int>(0.0067f * m_sr + 0.5f), 1, COMB_BUF_LEN - 1);
        m_d3 = std::clamp(static_cast<int>(0.0131f * m_sr + 0.5f), 1, COMB_BUF_LEN - 1);

     
        if (!m_formula.empty()) {
            setFormula(m_formula.c_str());
        }
    }

    void setFormula(const char* formula) {
        m_formula = formula ? formula : "";
        if (m_formula.empty()) {
            enabled = false;
            return;
        }
        enabled = true;

        // Auto-engage zero-latency biquad engine for the default harmonium cabinet model
        if (m_formula == defaultCombinedHarmoniumTf()) {
            m_useFastPath = true;
            resetFastState();
            return;
        }

        m_useFastPath = false;
        SimpleParser parser;
        parser.setExpression(m_formula);

        const float binWidth = m_sr * (1.0f / static_cast<float>(FFT_SIZE));
        // Pre-fold IFFT (1/N) and WOLA overlap-add gain (2 * HOP / N) into the filter curve
        const float combinedScale = (2.0f * static_cast<float>(HOP_SIZE) / static_cast<float>(FFT_SIZE))
                                  * (1.0f / static_cast<float>(FFT_SIZE));

        for (int i = 0; i <= HALF_FFT; ++i) {
            float freq = std::max(0.1f, i * binWidth);
            Complex H = parser.eval(freq) * combinedScale;
            if (i == 0 || i == HALF_FFT) {
                H = Complex(H.real(), 0.0f);
            }
            m_filterCurve[i] = H;
        }
        resetFftState();
    }

    inline float process(float input) {
        if (!enabled) return input;
        return m_useFastPath ? processFast(input) : processFft(input);
    }

    inline void processBlock(float* buf, int frames) {
        if (!enabled || !buf || frames <= 0) return;
        if (m_useFastPath) {
            for (int i = 0; i < frames; ++i) buf[i] = processFast(buf[i]);
        } else {
            for (int i = 0; i < frames; ++i) buf[i] = processFft(buf[i]);
        }
    }

    bool isUsingFastPath() const { return m_useFastPath; }
    void forceFftMode(bool force) {
        m_useFastPath = !force;
        if (force) setFormula(m_formula.c_str());
        m_useFastPath = !force;
    }

    bool enabled = true;

private:
    static constexpr int COMB_BUF_LEN = 512;
    static constexpr int COMB_MASK    = COMB_BUF_LEN - 1;

    struct FastBiquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() { z1 = 0.0f; z2 = 0.0f; }

        void setBandpass(float f0, float bwHz, float sr) {
            f0 = std::clamp(f0, 20.0f, sr * 0.42f);
            float w0 = 2.0f * M_PI_F * f0 / sr;
            float cosW0 = std::cos(w0);
            float sinW0 = std::sin(w0);
            float q = std::max(0.5f, f0 / std::max(10.0f, bwHz));
            float alpha = sinW0 / (2.0f * q);
            float invA0 = 1.0f / (1.0f + alpha);

            b0 = alpha * invA0;
            b1 = 0.0f;
            b2 = -alpha * invA0;
            a1 = (-2.0f * cosW0) * invA0;
            a2 = (1.0f - alpha) * invA0;
        }

        void setLowpass(float f0, float q, float sr) {
            f0 = std::clamp(f0, 100.0f, sr * 0.45f);
            float w0 = 2.0f * M_PI_F * f0 / sr;
            float cosW0 = std::cos(w0);
            float sinW0 = std::sin(w0);
            float alpha = sinW0 / (2.0f * q);
            float invA0 = 1.0f / (1.0f + alpha);

            b1 = (1.0f - cosW0) * invA0;
            b0 = 0.5f * b1;
            b2 = b0;
            a1 = (-2.0f * cosW0) * invA0;
            a2 = (1.0f - alpha) * invA0;
        }

        inline float process(float x) {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void initTables() {
        // 9-bit reversal for FFT_SIZE = 512
        for (int i = 0; i < FFT_SIZE; ++i) {
            int rev = 0;
            for (int j = 0; j < 9; ++j) {
                if (i & (1 << j)) rev |= (1 << (8 - j));
            }
            m_bitRev[i] = static_cast<uint16_t>(rev);
        }

        // Precompute unit-circle twiddles exp(+j * 2 * pi * k / FFT_SIZE)
        for (int k = 0; k < HALF_FFT; ++k) {
            float angle = 2.0f * M_PI_F * static_cast<float>(k) * (1.0f / static_cast<float>(FFT_SIZE));
            m_twiddleLut[k] = Complex(std::cos(angle), std::sin(angle));
        }

        // Root-Hann (Sine) window for dual analysis + synthesis WOLA
        for (int i = 0; i < FFT_SIZE; ++i) {
            m_window[i] = std::sin(M_PI_F * static_cast<float>(i) * (1.0f / static_cast<float>(FFT_SIZE)));
        }
    }

    void executeFft(bool inverse) {
        for (int i = 0; i < FFT_SIZE; ++i) {
            int j = m_bitRev[i];
            if (i < j) std::swap(m_fftBuffer[i], m_fftBuffer[j]);
        }

        for (int len = 2; len <= FFT_SIZE; len <<= 1) {
            const int half = len >> 1;
            const int step = FFT_SIZE / len;
            for (int i = 0; i < FFT_SIZE; i += len) {
                for (int j = 0; j < half; ++j) {
                    Complex w = m_twiddleLut[j * step];
                    if (inverse) w = Complex(w.real(), -w.imag());
                    Complex u = m_fftBuffer[i + j];
                    Complex v = fastComplexMul(m_fftBuffer[i + j + half], w);
                    m_fftBuffer[i + j]        = u + v;
                    m_fftBuffer[i + j + half] = u - v;
                }
            }
        }
    }

    void resetFastState() {
        m_bp95.reset();
        m_bp320.reset();
        m_bp1300.reset();
        m_lp7200.reset();
        std::memset(m_combBuf, 0, sizeof(m_combBuf));
        m_combIdx = 0;
        m_silentSamples = FFT_SIZE * 2;
    }

    void resetFftState() {
        std::memset(m_history, 0, sizeof(m_history));
        std::memset(m_overlapAdd, 0, sizeof(m_overlapAdd));
        std::memset(m_inputQueue, 0, sizeof(m_inputQueue));
        std::memset(m_outputQueue, 0, sizeof(m_outputQueue));
        m_hopPos = 0;
        m_silentSamples = FFT_SIZE * 2;
    }

    inline float processFast(float input) {
        if (std::fabs(input) < 1e-6f) {
            if (m_silentSamples < FFT_SIZE * 2) {
                if (++m_silentSamples == FFT_SIZE * 2) {
                    resetFastState();
                    return 0.0f;
                }
            } else {
                return 0.0f;
            }
        } else {
            m_silentSamples = 0;
        }

        // Tiny anti-denormal bias prevents FPU subnormal stalls during exponential release
        const float x = input + 1e-20f;
        float eq = x
                 + 0.40f * m_bp95.process(x)
                 + 2.50f * m_bp320.process(x)
                 + 1.20f * m_bp1300.process(x);

        eq = m_lp7200.process(eq);

        m_combBuf[m_combIdx] = eq;
        const float r1 = m_combBuf[(m_combIdx - m_d1) & COMB_MASK];
        const float r2 = m_combBuf[(m_combIdx - m_d2) & COMB_MASK];
        const float r3 = m_combBuf[(m_combIdx - m_d3) & COMB_MASK];
        m_combIdx = (m_combIdx + 1) & COMB_MASK;

        return (eq + 0.35f * r1 - 0.22f * r2 + 0.16f * r3) * 0.42f;
    }

    float processFft(float input) {
        if (std::fabs(input) < 1e-6f) {
            if (m_silentSamples < FFT_SIZE * 2) {
                if (++m_silentSamples == FFT_SIZE * 2) {
                    resetFftState();
                    return 0.0f;
                }
            } else {
                return 0.0f;
            }
        } else {
            m_silentSamples = 0;
        }

        float out = m_outputQueue[m_hopPos];
        m_inputQueue[m_hopPos++] = input;

        if (m_hopPos >= HOP_SIZE) {
            m_hopPos = 0;

            std::memmove(m_history, m_history + HOP_SIZE, (FFT_SIZE - HOP_SIZE) * sizeof(float));
            std::memcpy(m_history + (FFT_SIZE - HOP_SIZE), m_inputQueue, HOP_SIZE * sizeof(float));

            // Analysis Root-Hann window
            for (int i = 0; i < FFT_SIZE; ++i) {
                m_fftBuffer[i] = Complex(m_history[i] * m_window[i], 0.0f);
            }

            executeFft(false);

            // Apply pre-scaled complex transfer function H(f)
            for (int i = 0; i <= HALF_FFT; ++i) {
                m_fftBuffer[i] = fastComplexMul(m_fftBuffer[i], m_filterCurve[i]);
                if (i > 0 && i < HALF_FFT) {
                    m_fftBuffer[FFT_SIZE - i] = std::conj(m_fftBuffer[i]);
                }
            }

            executeFft(true);

            // Synthesis Root-Hann window (suppresses circular-convolution wrap artifacts)
            for (int i = 0; i < FFT_SIZE; ++i) {
                m_overlapAdd[i] += m_fftBuffer[i].real() * m_window[i];
            }

            std::memcpy(m_outputQueue, m_overlapAdd, HOP_SIZE * sizeof(float));
            std::memmove(m_overlapAdd, m_overlapAdd + HOP_SIZE, (FFT_SIZE - HOP_SIZE) * sizeof(float));
            std::memset(m_overlapAdd + (FFT_SIZE - HOP_SIZE), 0, HOP_SIZE * sizeof(float));
        }

        return out;
    }

    float m_sr = 22050.0f;
    std::string m_formula;
    bool m_useFastPath = true;

    // Fast Biquad + Comb State
    FastBiquad m_bp95, m_bp320, m_bp1300, m_lp7200;
    float m_combBuf[COMB_BUF_LEN] = {0.0f};
    int m_combIdx = 0;
    int m_d1 = 44, m_d2 = 148, m_d3 = 289;

    // Contiguous Fixed-Size FFT State (Zero Heap Indirection)
    int m_hopPos = 0;
    int m_silentSamples = FFT_SIZE * 2;
    uint16_t m_bitRev[FFT_SIZE] = {0};
    Complex m_twiddleLut[HALF_FFT];
    Complex m_filterCurve[HALF_FFT + 1];
    Complex m_fftBuffer[FFT_SIZE];
    float m_window[FFT_SIZE] = {0.0f};
    float m_history[FFT_SIZE] = {0.0f};
    float m_overlapAdd[FFT_SIZE] = {0.0f};
    float m_inputQueue[HOP_SIZE] = {0.0f};
    float m_outputQueue[HOP_SIZE] = {0.0f};
};

#endif // EFFECT_H
