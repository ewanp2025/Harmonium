#ifndef EFFECT_H
#define EFFECT_H

#include <complex>
#include <string>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

namespace {
using Complex = std::complex<float>;

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
    static constexpr int FFT_SIZE = 512;
    static constexpr int HOP_SIZE = 128;
    static constexpr int HALF_FFT = FFT_SIZE / 2;

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

        // 1. Configure Fixed-Point Biquads (Q14 coefficients: 16384 = 1.0)
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

        if (m_formula == defaultCombinedHarmoniumTf()) {
            m_useFastPath = true;
            resetFastState();
            return;
        }

        m_useFastPath = false;
        SimpleParser parser;
        parser.setExpression(m_formula);

        const float binWidth = m_sr * (1.0f / static_cast<float>(FFT_SIZE));
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

    // Pure 32-bit fixed-point path (inputQ14: 16384 = 1.0)
    inline int32_t processQ14(int32_t inputQ14) {
        if (!enabled) return inputQ14;
        if (!m_useFastPath) {
            float outF = processFft(static_cast<float>(inputQ14) * (1.0f / 16384.0f));
            return static_cast<int32_t>(outF * 16384.0f);
        }

        if (inputQ14 > -2 && inputQ14 < 2) {
            if (m_silentSamples < FFT_SIZE * 2) {
                if (++m_silentSamples == FFT_SIZE * 2) {
                    resetFastState();
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            m_silentSamples = 0;
        }

        const int32_t x = std::clamp<int32_t>(inputQ14, -32768, 32767);
        const int32_t b1 = m_bp95.processBandpassQ14(x);
        const int32_t b2 = m_bp320.processBandpassQ14(x);
        const int32_t b3 = m_bp1300.processBandpassQ14(x);

        // Sum parallel formant branches with pre-folded 0.42 unity gain in Q14:
        // Dry: 1.0*0.42 = 6881, BP95: 0.4*0.42 = 2753, BP320: 2.5*0.42 = 17203, BP1300: 1.2*0.42 = 8258
        int32_t eqQ14 = (6881 * x + 2753 * b1 + 17203 * b2 + 8258 * b3) >> 14;
        eqQ14 = std::clamp<int32_t>(eqQ14, -32768, 32767);

        eqQ14 = m_lp7200.processLowpassQ14(eqQ14);
        eqQ14 = std::clamp<int32_t>(eqQ14, -32768, 32767);

        m_combBuf[m_combIdx] = static_cast<int16_t>(eqQ14);
        const int32_t r1 = m_combBuf[(m_combIdx - m_d1) & COMB_MASK];
        const int32_t r2 = m_combBuf[(m_combIdx - m_d2) & COMB_MASK];
        const int32_t r3 = m_combBuf[(m_combIdx - m_d3) & COMB_MASK];
        m_combIdx = (m_combIdx + 1) & COMB_MASK;

        // Early reflections: +0.35*r1 (5734) - 0.22*r2 (3604) + 0.16*r3 (2621) in Q14
        return eqQ14 + ((5734 * r1 - 3604 * r2 + 2621 * r3) >> 14);
    }

    inline float process(float input) {
        if (!enabled) return input;
        if (m_useFastPath) {
            int32_t inQ14 = static_cast<int32_t>(input * 16384.0f);
            return static_cast<float>(processQ14(inQ14)) * (1.0f / 16384.0f);
        }
        return processFft(input);
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

    // Pure 32-bit Fixed-Point Direct Form I Biquad (Q14 coefficients: 16384 = 1.0)
    struct FixedBiquadQ14 {
        int32_t b0 = 16384, b1 = 0, b2 = 0;
        int32_t a1 = 0, a2 = 0;
        int32_t x1 = 0, x2 = 0;
        int32_t y1 = 0, y2 = 0;

        void reset() { x1 = x2 = y1 = y2 = 0; }

        void setBandpass(float f0, float bwHz, float sr) {
            f0 = std::clamp(f0, 20.0f, sr * 0.42f);
            float w0 = 2.0f * M_PI_F * f0 / sr;
            float cosW0 = std::cos(w0);
            float sinW0 = std::sin(w0);
            float q = std::max(0.5f, f0 / std::max(10.0f, bwHz));
            float alpha = sinW0 / (2.0f * q);
            float invA0 = 1.0f / (1.0f + alpha);

            b0 = static_cast<int32_t>(std::lround((alpha * invA0) * 16384.0f));
            b1 = 0;
            b2 = -b0;
            a1 = static_cast<int32_t>(std::lround(((-2.0f * cosW0) * invA0) * 16384.0f));
            a2 = static_cast<int32_t>(std::lround(((1.0f - alpha) * invA0) * 16384.0f));
        }

        void setLowpass(float f0, float q, float sr) {
            f0 = std::clamp(f0, 100.0f, sr * 0.45f);
            float w0 = 2.0f * M_PI_F * f0 / sr;
            float cosW0 = std::cos(w0);
            float sinW0 = std::sin(w0);
            float alpha = sinW0 / (2.0f * q);
            float invA0 = 1.0f / (1.0f + alpha);

            float fb1 = (1.0f - cosW0) * invA0;
            float fb0 = 0.5f * fb1;
            b0 = static_cast<int32_t>(std::lround(fb0 * 16384.0f));
            b1 = static_cast<int32_t>(std::lround(fb1 * 16384.0f));
            b2 = b0;
            a1 = static_cast<int32_t>(std::lround(((-2.0f * cosW0) * invA0) * 16384.0f));
            a2 = static_cast<int32_t>(std::lround(((1.0f - alpha) * invA0) * 16384.0f));
        }

        // Bandpass exploits b1 == 0 and b2 == -b0 (only 3 multiplies!)
        inline int32_t processBandpassQ14(int32_t x) {
            int32_t acc = b0 * (x - x2) - a1 * y1 - a2 * y2;
            int32_t y = std::clamp<int32_t>(acc >> 14, -32768, 32767);
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        // Lowpass exploits b0 == b2 (only 4 multiplies!)
        inline int32_t processLowpassQ14(int32_t x) {
            int32_t acc = b0 * (x + x2) + b1 * x1 - a1 * y1 - a2 * y2;
            int32_t y = std::clamp<int32_t>(acc >> 14, -32768, 32767);
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    void initTables() {
        for (int i = 0; i < FFT_SIZE; ++i) {
            int rev = 0;
            for (int j = 0; j < 9; ++j) {
                if (i & (1 << j)) rev |= (1 << (8 - j));
            }
            m_bitRev[i] = static_cast<uint16_t>(rev);
        }

        for (int k = 0; k < HALF_FFT; ++k) {
            float angle = 2.0f * M_PI_F * static_cast<float>(k) * (1.0f / static_cast<float>(FFT_SIZE));
            m_twiddleLut[k] = Complex(std::cos(angle), std::sin(angle));
        }

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

            for (int i = 0; i < FFT_SIZE; ++i) {
                m_fftBuffer[i] = Complex(m_history[i] * m_window[i], 0.0f);
            }

            executeFft(false);

            for (int i = 0; i <= HALF_FFT; ++i) {
                m_fftBuffer[i] = fastComplexMul(m_fftBuffer[i], m_filterCurve[i]);
                if (i > 0 && i < HALF_FFT) {
                    m_fftBuffer[FFT_SIZE - i] = std::conj(m_fftBuffer[i]);
                }
            }

            executeFft(true);

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

    // Fixed-Point Fast Path State (int32_t biquads + int16_t comb buffer)
    FixedBiquadQ14 m_bp95, m_bp320, m_bp1300, m_lp7200;
    int16_t m_combBuf[COMB_BUF_LEN] = {0};
    int m_combIdx = 0;
    int m_d1 = 44, m_d2 = 148, m_d3 = 289;

    // Contiguous FFT State (Fallback for arbitrary transfer function strings)
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
