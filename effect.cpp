#include "effect.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

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

namespace {
using Complex = std::complex<float>;
const float PI_LOGIC = 3.14159265358979f;

void inplace_fft(std::vector<Complex>& a, const std::vector<int>& bitRev,
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
    size_t pos;
    float currentFreq;

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
            size_t start = pos;
            while (pos < str.length() && (std::isdigit(static_cast<unsigned char>(str[pos])) || str[pos] == '.')) pos++;
            float val = std::stof(str.substr(start, pos - start));
            if (peek() == 'j') { get(); return Complex(0.0f, val); }
            return Complex(val, 0.0f);
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::string token;
            while (pos < str.length() && std::isalpha(static_cast<unsigned char>(str[pos]))) token += str[pos++];

            if (token == "freq") return Complex(currentFreq, 0.0f);
            if (token == "w")    return Complex(2.0f * PI_LOGIC * currentFreq, 0.0f);
            if (token == "s")    return Complex(0.0f, 2.0f * PI_LOGIC * currentFreq);
            if (token == "j")    return Complex(0.0f, 1.0f);
            if (token == "pi")   return Complex(PI_LOGIC, 0.0f);

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
        while (true) {
            if (peek() == '^') {
                get();
                Complex rhs = parseUnary();
                lhs = std::pow(lhs, rhs);
            } else {
                break;
            }
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
}

TransferFunctionFX::TransferFunctionFX(double sampleRate) {
    setSampleRate(sampleRate);
}

void TransferFunctionFX::setSampleRate(double sr) {
    m_sr = std::max(8000.0, sr);
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
        float angleFwd = 2.0f * PI_LOGIC / static_cast<float>(len);
        float angleInv = -angleFwd;
        m_twiddlesFwd.emplace_back(std::cos(angleFwd), std::sin(angleFwd));
        m_twiddlesInv.emplace_back(std::cos(angleInv), std::sin(angleInv));
    }

    m_fftBuffer.assign(m_fftSize, Complex(0.0f, 0.0f));
    m_window.assign(m_fftSize, 0.0f);
    for (int i = 0; i < m_fftSize; ++i) {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / m_fftSize));
    }

    m_history.assign(m_fftSize, 0.0f);
    m_overlapAdd.assign(m_fftSize, 0.0f);
    m_inputQueue.assign(m_hopSize, 0.0f);
    m_outputQueue.assign(m_hopSize, 0.0f);

    setFormula(m_formula);
}

void TransferFunctionFX::setFormula(const QString& formula) {
    m_formula = formula;
    if (m_formula.trimmed().isEmpty()) {
        enabled = false;
        return;
    }
    enabled = true;
    SimpleParser parser;
    float binWidth = static_cast<float>(m_sr) / static_cast<float>(m_fftSize);
    m_filterCurve.assign(m_fftSize / 2 + 1, Complex(1.0f, 0.0f));
    for (int i = 0; i <= m_fftSize / 2; ++i) {
        float freq = i * binWidth;
        if (freq < 0.1f) freq = 0.1f;
        m_filterCurve[i] = parser.eval(m_formula.toStdString(), freq);
    }
}

float TransferFunctionFX::process(float input) {
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
    m_inputQueue[m_hopPos] = input;
    m_hopPos++;

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