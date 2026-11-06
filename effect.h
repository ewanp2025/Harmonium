#ifndef EFFECT_H
#define EFFECT_H

#include <vector>
#include <complex>
#include <string>
#include <cmath>
#include <cctype>
#include <cstring>
#include <algorithm>

#ifndef M_PI_F
#define M_PI_F 3.14159265358979f
#endif

namespace {
using Complex = std::complex<float>;

inline void inplace_fft(std::vector<Complex>& a, const std::vector<int>& bitRev,
                        const std::vector<Complex>& stageTwiddles, bool inverse) {
    const int n = static_cast<int>(a.size());
    for (int i = 0; i < n; ++i) {
        int j = bitRev[i];
        if (i < j) std::swap(a[i], a[j]);
    }

    int stageIdx = 0;
    for (int len = 2; len <= n; len <<= 1) {
        Complex wlen = stageTwiddles[stageIdx++];
        int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            Complex w(1.0f, 0.0f);
            for (int j = 0; j < half; ++j) {
                Complex u = a[i + j];
                Complex v = a[i + j + half] * w;
                a[i + j] = u + v;
                a[i + j + half] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        const float invN = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            a[i] *= invN;
        }
    }
}

class SimpleParser {
    std::string str;
    size_t pos = 0;
    float currentFreq = 1.0f;

    char peek() {
        while (pos < str.length() && std::isspace(static_cast<unsigned char>(str[pos]))) pos++;
        if (pos >= str.length()) return 0;
        return str[pos];
    }

    char get() {
        char c = peek();
        if (pos < str.length()) pos++;
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
            const char* startPtr = str.c_str() + pos;
            char* endPtr = nullptr;
            float val = std::strtof(startPtr, &endPtr);
            pos = static_cast<size_t>(endPtr - str.c_str());
            if (peek() == 'j') { get(); return Complex(0.0f, val); }
            return Complex(val, 0.0f);
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string token;
            while (pos < str.length() && std::isalpha(static_cast<unsigned char>(str[pos]))) token += str[pos++];

            if (token == "freq") return Complex(currentFreq, 0.0f);
            if (token == "w")    return Complex(2.0f * M_PI_F * currentFreq, 0.0f);
            if (token == "s")    return Complex(0.0f, 2.0f * M_PI_F * currentFreq);
            if (token == "j")    return Complex(0.0f, 1.0f);
            if (token == "pi")   return Complex(M_PI_F, 0.0f);

            if (peek() == '(') {
                get();
                Complex arg = parseExpression();
                if (peek() == ')') get();

                if (token == "sqrt") return std::sqrt(arg);
                if (token == "exp")  return std::exp(arg);
                if (token == "sin")  return std::sin(arg);
                if (token == "cos")  return std::cos(arg);
                if (token == "tan")  return std::tan(arg);
                if (token == "abs")  return Complex(std::abs(arg), 0.0f);
                if (token == "log")  return std::log(arg);
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
            if (op == '*') { get(); lhs *= parseUnary(); }
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
    Complex eval(const std::string& expression, float f) {
        str = expression;
        pos = 0;
        currentFreq = f;
        if (str.empty()) return Complex(1.0f, 0.0f);
        return parseExpression();
    }
};
} // namespace

class TransferFunctionFX {
public:
    static const char* defaultCombinedHarmoniumTf() {
        return "((1 + 0.4 * exp(-(((freq-95)/55)^2)) + 2.5 * exp(-(((freq-320)/100)^2)) + 1.2 * exp(-(((freq-1300)/200)^2))) / sqrt(1 + (freq/7200)^8)) * "
               "(1 + 0.35 * exp(-j * 2 * pi * freq * 0.0020) - 0.22 * exp(-j * 2 * pi * freq * 0.0067) + 0.16 * exp(-j * 2 * pi * freq * 0.0131))";
    }

    explicit TransferFunctionFX(float sampleRate = 22050.0f) {
        m_formula = defaultCombinedHarmoniumTf();
        setSampleRate(sampleRate);
    }

    void setSampleRate(float sr) {
        m_sr = std::max(8000.0f, sr);
        m_fftSize = 512;
        m_hopSize = 128;
        m_hopPos = 0;
        m_silentSamples = m_fftSize * 2;

        m_bitRev.assign(m_fftSize, 0);
        int logN = 0;
        while ((1 << logN) < m_fftSize) logN++;
        for (int i = 0; i < m_fftSize; ++i) {
            int rev = 0;
            for (int j = 0; j < logN; ++j) {
                if (i & (1 << j)) rev |= (1 << (logN - 1 - j));
            }
            m_bitRev[i] = rev;
        }

        m_twiddlesFwd.clear();
        m_twiddlesInv.clear();
        for (int len = 2; len <= m_fftSize; len <<= 1) {
            float angleFwd = 2.0f * M_PI_F / static_cast<float>(len);
            float angleInv = -angleFwd;
            m_twiddlesFwd.emplace_back(std::cos(angleFwd), std::sin(angleFwd));
            m_twiddlesInv.emplace_back(std::cos(angleInv), std::sin(angleInv));
        }

        m_fftBuffer.assign(m_fftSize, Complex(0.0f, 0.0f));
        m_window.assign(m_fftSize, 0.0f);
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI_F * i / m_fftSize));
        }

        m_history.assign(m_fftSize, 0.0f);
        m_overlapAdd.assign(m_fftSize, 0.0f);
        m_inputQueue.assign(m_hopSize, 0.0f);
        m_outputQueue.assign(m_hopSize, 0.0f);

        setFormula(m_formula.c_str());
    }

    void setFormula(const char* formula) {
        m_formula = formula ? formula : "";
        if (m_formula.empty()) {
            enabled = false;
            return;
        }
        enabled = true;
        SimpleParser parser;
        float binWidth = m_sr / static_cast<float>(m_fftSize);
        m_filterCurve.assign(m_fftSize / 2 + 1, Complex(1.0f, 0.0f));
        for (int i = 0; i <= m_fftSize / 2; ++i) {
            float freq = std::max(0.1f, i * binWidth);
            m_filterCurve[i] = parser.eval(m_formula, freq);
        }
    }

    float process(float input) {
        if (!enabled) return input;

        if (std::fabs(input) < 1e-6f) {
            if (m_silentSamples < m_fftSize * 2) {
                m_silentSamples++;
                if (m_silentSamples == m_fftSize * 2) {
                    std::fill(m_history.begin(), m_history.end(), 0.0f);
                    std::fill(m_overlapAdd.begin(), m_overlapAdd.end(), 0.0f);
                    std::fill(m_inputQueue.begin(), m_inputQueue.end(), 0.0f);
                    std::fill(m_outputQueue.begin(), m_outputQueue.end(), 0.0f);
                    m_hopPos = 0;
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

        if (m_hopPos >= m_hopSize) {
            m_hopPos = 0;

            std::memmove(m_history.data(), m_history.data() + m_hopSize, (m_fftSize - m_hopSize) * sizeof(float));
            std::memcpy(m_history.data() + (m_fftSize - m_hopSize), m_inputQueue.data(), m_hopSize * sizeof(float));

            for (int i = 0; i < m_fftSize; ++i) {
                m_fftBuffer[i] = Complex(m_history[i] * m_window[i], 0.0f);
            }

            inplace_fft(m_fftBuffer, m_bitRev, m_twiddlesFwd, false);

            const int half = m_fftSize / 2;
            for (int i = 0; i <= half; ++i) {
                Complex H = m_filterCurve[i];
                if (i == 0 || i == half) H = Complex(H.real(), 0.0f);
                m_fftBuffer[i] *= H;
                if (i > 0 && i < half) {
                    m_fftBuffer[m_fftSize - i] = std::conj(m_fftBuffer[i]);
                }
            }

            inplace_fft(m_fftBuffer, m_bitRev, m_twiddlesInv, true);

            const float outputScale = 2.0f * static_cast<float>(m_hopSize) / static_cast<float>(m_fftSize);
            for (int i = 0; i < m_fftSize; ++i) {
                m_overlapAdd[i] += m_fftBuffer[i].real() * outputScale;
            }

            std::memcpy(m_outputQueue.data(), m_overlapAdd.data(), m_hopSize * sizeof(float));
            std::memmove(m_overlapAdd.data(), m_overlapAdd.data() + m_hopSize, (m_fftSize - m_hopSize) * sizeof(float));
            std::memset(m_overlapAdd.data() + (m_fftSize - m_hopSize), 0, m_hopSize * sizeof(float));
        }

        return out;
    }

    bool enabled = true;

private:
    float m_sr = 22050.0f;
    std::string m_formula;
    int m_fftSize = 512;
    int m_hopSize = 128;
    int m_hopPos = 0;
    int m_silentSamples = 1024;

    std::vector<int> m_bitRev;
    std::vector<Complex> m_twiddlesFwd;
    std::vector<Complex> m_twiddlesInv;
    std::vector<Complex> m_filterCurve;
    std::vector<Complex> m_fftBuffer;

    std::vector<float> m_window;
    std::vector<float> m_history;
    std::vector<float> m_overlapAdd;
    std::vector<float> m_inputQueue;
    std::vector<float> m_outputQueue;
};

#endif // EFFECT_H
