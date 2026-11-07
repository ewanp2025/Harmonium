#ifndef XPRESSIVE_PARSER_H
#define XPRESSIVE_PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#ifndef M_PI_F
#define M_PI_F 3.14159265f
#endif

namespace XpressiveParser {

struct SineLUT {
    float   table[2048];
    int16_t sinQ14[2048];       // Q14 (16384 = 1.0): sin(x)
    int16_t reedBaseQ14[2048];  // Q14: sin(x) - 0.5*sin(2x) + (1/3)*sin(3x)
    int16_t reedUpperQ14[2048]; // Q14: (1/3)*sin(3x) - 0.25*sin(4x) + 0.20*sin(5x)

    SineLUT() {
        for (int i = 0; i < 2048; ++i) {
            float th = 2.0f * M_PI_F * (static_cast<float>(i) / 2048.0f);
            float s1 = std::sin(th);
            float s2 = std::sin(2.0f * th);
            float s3 = std::sin(3.0f * th);
            float s4 = std::sin(4.0f * th);
            float s5 = std::sin(5.0f * th);

            float rBase  = s1 - 0.5f * s2 + (1.0f / 3.0f) * s3;
            float rUpper = (1.0f / 3.0f) * s3 - 0.25f * s4 + 0.20f * s5;

            table[i]        = s1;
            sinQ14[i]       = static_cast<int16_t>(std::clamp(s1 * 16384.0f, -32767.0f, 32767.0f));
            reedBaseQ14[i]  = static_cast<int16_t>(std::clamp(rBase * 16384.0f, -32767.0f, 32767.0f));
            reedUpperQ14[i] = static_cast<int16_t>(std::clamp(rUpper * 16384.0f, -32767.0f, 32767.0f));
        }
    }


    inline int32_t sinUnitQ14(uint32_t phaseQ32) const {
        return sinQ14[phaseQ32 >> 21];
    }
    inline int32_t reedBaseUnitQ14(uint32_t phaseQ32) const {
        return reedBaseQ14[phaseQ32 >> 21];
    }
    inline int32_t reedUpperUnitQ14(uint32_t phaseQ32) const {
        return reedUpperQ14[phaseQ32 >> 21];
    }

    
    inline float sinUnit(float x) const {
        int idx = static_cast<int>(x * 2048.0f) & 2047;
        return table[idx];
    }
    inline float sinRad(float rad) const {
        return sinUnit(rad * (1.0f / (2.0f * M_PI_F)));
    }
    inline float cosRad(float rad) const {
        return sinUnit(rad * (1.0f / (2.0f * M_PI_F)) + 0.25f);
    }
};

inline const SineLUT& lut() {
    static SineLUT s_lut;
    return s_lut;
}

class Node;

struct Env {
    float t = 0.0f;
    float tempo = 120.0f;
    float srate = 22050.0f;
    float f = 440.0f;
    float rel = 0.0f;
    float trel = 0.0f;
    float A1 = 1.0f, A2 = 1.0f, A3 = 1.0f;
    float lastOut = 0.0f;

    float* intState = nullptr;
    int numIntegrators = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual float eval(Env& env) const = 0;
};

enum Token {
    TOK_EOF, TOK_NUM, TOK_ID,
    TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_OR, TOK_AND, TOK_QUESTION, TOK_COLON
};

struct Lexer {
    const char* p;
    Token curTok;
    float numVal = 0.0f;
    std::string idStr;
    int integratorCount = 0;

    explicit Lexer(const char* str) : p(str) { next(); }

    void next() {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) { curTok = TOK_EOF; return; }
        if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.') {
            char* end = nullptr;
            numVal = std::strtof(p, &end);
            p = end;
            curTok = TOK_NUM;
            return;
        }
        if (std::isalpha(static_cast<unsigned char>(*p)) || *p == '_') {
            idStr.clear();
            while (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_') idStr += *p++;
            curTok = TOK_ID;
            return;
        }
        char c = *p++;
        if (c == '=') { if (*p == '=') { p++; curTok = TOK_EQ; } else curTok = TOK_EOF; }
        else if (c == '!') { if (*p == '=') { p++; curTok = TOK_NEQ; } else curTok = TOK_EOF; }
        else if (c == '<') { if (*p == '=') { p++; curTok = TOK_LTE; } else curTok = TOK_LT; }
        else if (c == '>') { if (*p == '=') { p++; curTok = TOK_GTE; } else curTok = TOK_GT; }
        else if (c == '|') { if (*p == '|') p++; curTok = TOK_OR; }
        else if (c == '&') { if (*p == '&') p++; curTok = TOK_AND; }
        else if (c == '?') curTok = TOK_QUESTION;
        else if (c == ':') curTok = TOK_COLON;
        else if (c == '+') curTok = TOK_PLUS;
        else if (c == '-') curTok = TOK_MINUS;
        else if (c == '*') curTok = TOK_MUL;
        else if (c == '/') curTok = TOK_DIV;
        else if (c == '(') curTok = TOK_LPAREN;
        else if (c == ')') curTok = TOK_RPAREN;
        else if (c == ',') curTok = TOK_COMMA;
        else curTok = TOK_EOF;
    }
};

class NumNode : public Node {
    float val;
public:
    explicit NumNode(float v) : val(v) {}
    float eval(Env&) const override { return val; }
};

class LastNode : public Node {
public:
    float eval(Env& e) const override { return e.lastOut; }
};

class VarNode : public Node {
    enum VarType { V_T, V_TEMPO, V_SRATE, V_F, V_REL, V_TREL, V_A1, V_A2, V_A3, V_PI } type;
public:
    explicit VarNode(const std::string& n) {
        if (n == "t" || n == "lt") type = V_T;
        else if (n == "tempo") type = V_TEMPO;
        else if (n == "srate") type = V_SRATE;
        else if (n == "rel") type = V_REL;
        else if (n == "trel") type = V_TREL;
        else if (n == "A1") type = V_A1;
        else if (n == "A2") type = V_A2;
        else if (n == "A3") type = V_A3;
        else if (n == "pi" || n == "PI") type = V_PI;
        else type = V_F;
    }
    float eval(Env& e) const override {
        switch (type) {
            case V_T: return e.t;
            case V_TEMPO: return e.tempo;
            case V_SRATE: return e.srate;
            case V_REL: return e.rel;
            case V_TREL: return e.trel;
            case V_A1: return e.A1;
            case V_A2: return e.A2;
            case V_A3: return e.A3;
            case V_PI: return M_PI_F;
            default: return e.f;
        }
    }
};

class UnaryNode : public Node {
    std::unique_ptr<Node> n;
public:
    explicit UnaryNode(std::unique_ptr<Node> node) : n(std::move(node)) {}
    float eval(Env& e) const override { return -n->eval(e); }
};

class Func1Node : public Node {
    std::unique_ptr<Node> a;
    float (*fn)(float);
public:
    Func1Node(std::unique_ptr<Node> arg, float (*f)(float)) : a(std::move(arg)), fn(f) {}
    float eval(Env& e) const override { return fn(a->eval(e)); }
};

class Func2Node : public Node {
    std::unique_ptr<Node> a, b;
    float (*fn)(float, float);
public:
    Func2Node(std::unique_ptr<Node> a1, std::unique_ptr<Node> a2, float (*f)(float, float))
        : a(std::move(a1)), b(std::move(a2)), fn(f) {}
    float eval(Env& e) const override { return fn(a->eval(e), b->eval(e)); }
};

class ClampNode : public Node {
    std::unique_ptr<Node> l, m, r;
public:
    ClampNode(std::unique_ptr<Node> a1, std::unique_ptr<Node> a2, std::unique_ptr<Node> a3)
        : l(std::move(a1)), m(std::move(a2)), r(std::move(a3)) {}
    float eval(Env& e) const override {
        float minV = l->eval(e), val = m->eval(e), maxV = r->eval(e);
        return std::max(minV, std::min(val, maxV));
    }
};

class TernaryCondNode : public Node {
    std::unique_ptr<Node> cond, tBranch, fBranch;
public:
    TernaryCondNode(std::unique_ptr<Node> c, std::unique_ptr<Node> t, std::unique_ptr<Node> f)
        : cond(std::move(c)), tBranch(std::move(t)), fBranch(std::move(f)) {}
    float eval(Env& e) const override {
        return (cond->eval(e) != 0.0f) ? tBranch->eval(e) : fBranch->eval(e);
    }
};

class BinOpNode : public Node {
    std::unique_ptr<Node> l, r;
    Token op;
public:
    BinOpNode(std::unique_ptr<Node> left, std::unique_ptr<Node> right, Token tok)
        : l(std::move(left)), r(std::move(right)), op(tok) {}
    float eval(Env& e) const override {
        float a = l->eval(e), b = r->eval(e);
        switch (op) {
            case TOK_PLUS: return a + b;
            case TOK_MINUS: return a - b;
            case TOK_MUL: return a * b;
            case TOK_DIV: return b == 0.0f ? 0.0f : a / b;
            case TOK_EQ: return a == b ? 1.0f : 0.0f;
            case TOK_NEQ: return a != b ? 1.0f : 0.0f;
            case TOK_LT: return a < b ? 1.0f : 0.0f;
            case TOK_GT: return a > b ? 1.0f : 0.0f;
            case TOK_LTE: return a <= b ? 1.0f : 0.0f;
            case TOK_GTE: return a >= b ? 1.0f : 0.0f;
            case TOK_OR: return (a != 0.0f || b != 0.0f) ? 1.0f : 0.0f;
            case TOK_AND: return (a != 0.0f && b != 0.0f) ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }
};

class IntegrateNode : public Node {
    std::unique_ptr<Node> n;
    int slot;
    mutable float fallbackAccum = 0.0f;
public:
    IntegrateNode(std::unique_ptr<Node> node, int s) : n(std::move(node)), slot(s) {}
    float eval(Env& e) const override {
        float inc = n->eval(e) / e.srate;
        if (e.intState && slot < e.numIntegrators) {
            e.intState[slot] += inc;
            return e.intState[slot];
        }
        fallbackAccum += inc;
        return fallbackAccum;
    }
};

inline float fnSinew(float x)     { return lut().sinUnit(x); }
inline float fnSaww(float x)      { float p = x - std::floor(x); return 2.0f * p - 1.0f; }
inline float fnSquarew(float x)   { float p = x - std::floor(x); return p < 0.5f ? 1.0f : -1.0f; }
inline float fnTrianglew(float x) { float p = x - std::floor(x); return 1.0f - 4.0f * std::fabs(p - 0.5f); }
inline float fnRandv(float)       { return (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f; }
inline float fnMod(float a, float b) { return b == 0.0f ? 0.0f : std::fmod(a, b); }
inline float fnMin(float a, float b) { return std::min(a, b); }
inline float fnMax(float a, float b) { return std::max(a, b); }
inline float fnPow(float a, float b) { return std::pow(a, b); }
inline float fnSin(float a)       { return lut().sinRad(a); }
inline float fnCos(float a)       { return lut().cosRad(a); }
inline float fnExp(float a)       { return a < -20.0f ? 0.0f : std::exp(a); }
inline float fnFloor(float a)     { return std::floor(a); }
inline float fnAbs(float a)       { return std::fabs(a); }
inline float fnCent(float c)      { return std::pow(2.0f, c * (1.0f / 1200.0f)); }

inline std::unique_ptr<Node> parseExpr(Lexer& lex);

inline std::unique_ptr<Node> parsePrimary(Lexer& lex) {
    if (lex.curTok == TOK_NUM) {
        auto n = std::make_unique<NumNode>(lex.numVal);
        lex.next();
        return n;
    }
    if (lex.curTok == TOK_ID) {
        std::string id = lex.idStr;
        lex.next();
        if (lex.curTok != TOK_LPAREN) {
            return std::make_unique<VarNode>(id);
        }
        lex.next();
        auto a1 = parseExpr(lex);
        std::unique_ptr<Node> a2, a3;
        if (lex.curTok == TOK_COMMA) {
            lex.next();
            a2 = parseExpr(lex);
            if (lex.curTok == TOK_COMMA) {
                lex.next();
                a3 = parseExpr(lex);
            }
        }
        if (lex.curTok == TOK_RPAREN) lex.next();

        if (id == "clamp" && a2 && a3) return std::make_unique<ClampNode>(std::move(a1), std::move(a2), std::move(a3));
        if (id == "mod" && a2) return std::make_unique<Func2Node>(std::move(a1), std::move(a2), fnMod);
        if (id == "min" && a2) return std::make_unique<Func2Node>(std::move(a1), std::move(a2), fnMin);
        if (id == "max" && a2) return std::make_unique<Func2Node>(std::move(a1), std::move(a2), fnMax);
        if (id == "pow" && a2) return std::make_unique<Func2Node>(std::move(a1), std::move(a2), fnPow);
        if (id == "sinew") return std::make_unique<Func1Node>(std::move(a1), fnSinew);
        if (id == "saww") return std::make_unique<Func1Node>(std::move(a1), fnSaww);
        if (id == "squarew") return std::make_unique<Func1Node>(std::move(a1), fnSquarew);
        if (id == "trianglew") return std::make_unique<Func1Node>(std::move(a1), fnTrianglew);
        if (id == "sin") return std::make_unique<Func1Node>(std::move(a1), fnSin);
        if (id == "cos") return std::make_unique<Func1Node>(std::move(a1), fnCos);
        if (id == "exp") return std::make_unique<Func1Node>(std::move(a1), fnExp);
        if (id == "floor") return std::make_unique<Func1Node>(std::move(a1), fnFloor);
        if (id == "abs") return std::make_unique<Func1Node>(std::move(a1), fnAbs);
        if (id == "cent") return std::make_unique<Func1Node>(std::move(a1), fnCent);
        if (id == "last") return std::make_unique<LastNode>();
        if (id == "randv") return std::make_unique<Func1Node>(std::move(a1), fnRandv);
        if (id == "integrate") return std::make_unique<IntegrateNode>(std::move(a1), lex.integratorCount++);

        return std::make_unique<NumNode>(0.0f);
    }
    if (lex.curTok == TOK_LPAREN) {
        lex.next();
        auto n = parseExpr(lex);
        if (lex.curTok == TOK_RPAREN) lex.next();
        return n;
    }
    return std::make_unique<NumNode>(0.0f);
}

inline std::unique_ptr<Node> parseUnary(Lexer& lex) {
    if (lex.curTok == TOK_MINUS) { lex.next(); return std::make_unique<UnaryNode>(parseUnary(lex)); }
    if (lex.curTok == TOK_PLUS) { lex.next(); return parseUnary(lex); }
    return parsePrimary(lex);
}

inline std::unique_ptr<Node> parseMulDiv(Lexer& lex) {
    auto l = parseUnary(lex);
    while (lex.curTok == TOK_MUL || lex.curTok == TOK_DIV) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseUnary(lex), op);
    }
    return l;
}

inline std::unique_ptr<Node> parseAddSub(Lexer& lex) {
    auto l = parseMulDiv(lex);
    while (lex.curTok == TOK_PLUS || lex.curTok == TOK_MINUS) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseMulDiv(lex), op);
    }
    return l;
}

inline std::unique_ptr<Node> parseRel(Lexer& lex) {
    auto l = parseAddSub(lex);
    while (lex.curTok == TOK_LT || lex.curTok == TOK_GT || lex.curTok == TOK_LTE || lex.curTok == TOK_GTE) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseAddSub(lex), op);
    }
    return l;
}

inline std::unique_ptr<Node> parseEq(Lexer& lex) {
    auto l = parseRel(lex);
    while (lex.curTok == TOK_EQ || lex.curTok == TOK_NEQ) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseRel(lex), op);
    }
    return l;
}

inline std::unique_ptr<Node> parseAnd(Lexer& lex) {
    auto l = parseEq(lex);
    while (lex.curTok == TOK_AND) {
        lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseEq(lex), TOK_AND);
    }
    return l;
}

inline std::unique_ptr<Node> parseOr(Lexer& lex) {
    auto l = parseAnd(lex);
    while (lex.curTok == TOK_OR) {
        lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseAnd(lex), TOK_OR);
    }
    return l;
}

inline std::unique_ptr<Node> parseExpr(Lexer& lex) {
    auto cond = parseOr(lex);
    if (lex.curTok == TOK_QUESTION) {
        lex.next();
        auto tBranch = parseExpr(lex);
        if (lex.curTok == TOK_COLON) lex.next();
        auto fBranch = parseExpr(lex);
        return std::make_unique<TernaryCondNode>(std::move(cond), std::move(tBranch), std::move(fBranch));
    }
    return cond;
}

inline std::unique_ptr<Node> parse(const char* str, int* outIntegratorCount = nullptr) {
    if (!str || !*str) return std::make_unique<NumNode>(0.0f);
    Lexer lex(str);
    auto root = parseExpr(lex);
    if (outIntegratorCount) *outIntegratorCount = lex.integratorCount;
    return root;
}

} // namespace XpressiveParser

#endif // XPRESSIVE_PARSER_H
