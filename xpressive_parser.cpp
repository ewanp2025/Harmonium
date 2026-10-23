#include "xpressive_parser.h"
#include <cmath>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace XpressiveParser {

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
    double numVal = 0.0;
    std::string idStr;
    int integratorCount = 0;

    explicit Lexer(const char* str) : p(str) { next(); }

    void next() {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) { curTok = TOK_EOF; return; }
        if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.') {
            char* end = nullptr;
            numVal = std::strtod(p, &end);
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
    double val;
public:
    explicit NumNode(double v) : val(v) {}
    double eval(Env&) const override { return val; }
};

class LastNode : public Node {
public:
    double eval(Env& e) const override { return e.lastOut; }
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
    double eval(Env& e) const override {
        switch (type) {
            case V_T: return e.t;
            case V_TEMPO: return e.tempo;
            case V_SRATE: return e.srate;
            case V_REL: return e.rel;
            case V_TREL: return e.trel;
            case V_A1: return e.A1;
            case V_A2: return e.A2;
            case V_A3: return e.A3;
            case V_PI: return M_PI;
            default: return e.f;
        }
    }
};

class UnaryNode : public Node {
    std::unique_ptr<Node> n;
public:
    explicit UnaryNode(std::unique_ptr<Node> node) : n(std::move(node)) {}
    double eval(Env& e) const override { return -n->eval(e); }
};

class Func1Node : public Node {
    std::unique_ptr<Node> a;
    double (*fn)(double);
public:
    Func1Node(std::unique_ptr<Node> arg, double (*f)(double)) : a(std::move(arg)), fn(f) {}
    double eval(Env& e) const override { return fn(a->eval(e)); }
};

class Func2Node : public Node {
    std::unique_ptr<Node> a, b;
    double (*fn)(double, double);
public:
    Func2Node(std::unique_ptr<Node> a1, std::unique_ptr<Node> a2, double (*f)(double, double))
        : a(std::move(a1)), b(std::move(a2)), fn(f) {}
    double eval(Env& e) const override { return fn(a->eval(e), b->eval(e)); }
};

class ClampNode : public Node {
    std::unique_ptr<Node> l, m, r;
public:
    ClampNode(std::unique_ptr<Node> a1, std::unique_ptr<Node> a2, std::unique_ptr<Node> a3)
        : l(std::move(a1)), m(std::move(a2)), r(std::move(a3)) {}
    double eval(Env& e) const override {
        double minV = l->eval(e), val = m->eval(e), maxV = r->eval(e);
        return std::max(minV, std::min(val, maxV));
    }
};

class TernaryCondNode : public Node {
    std::unique_ptr<Node> cond, tBranch, fBranch;
public:
    TernaryCondNode(std::unique_ptr<Node> c, std::unique_ptr<Node> t, std::unique_ptr<Node> f)
        : cond(std::move(c)), tBranch(std::move(t)), fBranch(std::move(f)) {}
    double eval(Env& e) const override {
        return (cond->eval(e) != 0.0) ? tBranch->eval(e) : fBranch->eval(e);
    }
};

class BinOpNode : public Node {
    std::unique_ptr<Node> l, r;
    Token op;
public:
    BinOpNode(std::unique_ptr<Node> left, std::unique_ptr<Node> right, Token tok)
        : l(std::move(left)), r(std::move(right)), op(tok) {}
    double eval(Env& e) const override {
        double a = l->eval(e), b = r->eval(e);
        switch (op) {
            case TOK_PLUS: return a + b;
            case TOK_MINUS: return a - b;
            case TOK_MUL: return a * b;
            case TOK_DIV: return b == 0.0 ? 0.0 : a / b;
            case TOK_EQ: return a == b ? 1.0 : 0.0;
            case TOK_NEQ: return a != b ? 1.0 : 0.0;
            case TOK_LT: return a < b ? 1.0 : 0.0;
            case TOK_GT: return a > b ? 1.0 : 0.0;
            case TOK_LTE: return a <= b ? 1.0 : 0.0;
            case TOK_GTE: return a >= b ? 1.0 : 0.0;
            case TOK_OR: return (a != 0.0 || b != 0.0) ? 1.0 : 0.0;
            case TOK_AND: return (a != 0.0 && b != 0.0) ? 1.0 : 0.0;
            default: return 0.0;
        }
    }
};

class IntegrateNode : public Node {
    std::unique_ptr<Node> n;
    int slot;
    mutable double fallbackAccum = 0.0;
public:
    IntegrateNode(std::unique_ptr<Node> node, int s) : n(std::move(node)), slot(s) {}
    double eval(Env& e) const override {
        double inc = n->eval(e) / e.srate;
        if (e.intState && slot < e.numIntegrators) {
            e.intState[slot] += inc;
            return e.intState[slot];
        }
        fallbackAccum += inc;
        return fallbackAccum;
    }
};

class WaveCallNode : public Node {
    int idx;
    std::unique_ptr<Node> arg;
public:
    WaveCallNode(int wIdx, std::unique_ptr<Node> a) : idx(wIdx), arg(std::move(a)) {}
    double eval(Env& e) const override {
        double phase = arg->eval(e);
        const Node* targetNode = (idx == 1) ? e.w1 : (idx == 2) ? e.w2 : e.w3;
        if (targetNode) {
            double oldT = e.t;
            e.t = phase;
            double res = targetNode->eval(e);
            e.t = oldT;
            return res;
        }
        const std::vector<float>* sample = (idx == 1) ? e.w1Sample : (idx == 2) ? e.w2Sample : e.w3Sample;
        if (sample && !sample->empty()) {
            double p = phase - std::floor(phase);
            size_t pos = static_cast<size_t>(p * sample->size()) % sample->size();
            return (*sample)[pos];
        }
        return 0.0;
    }
};

static double fnSinew(double x) { return std::sin(2.0 * M_PI * x); }
static double fnSaww(double x) { double p = x - std::floor(x); return 2.0 * p - 1.0; }
static double fnSquarew(double x) { double p = x - std::floor(x); return p < 0.5 ? 1.0 : -1.0; }
static double fnTrianglew(double x) { double p = x - std::floor(x); return 1.0 - 4.0 * std::abs(p - 0.5); }
static double fnRandv(double) { return (static_cast<double>(std::rand()) / RAND_MAX) * 2.0 - 1.0; }
static double fnMod(double a, double b) { return b == 0.0 ? 0.0 : std::fmod(a, b); }
static double fnMin(double a, double b) { return std::min(a, b); }
static double fnMax(double a, double b) { return std::max(a, b); }
static double fnPow(double a, double b) { return std::pow(a, b); }
static double fnSin(double a) { return std::sin(a); }
static double fnCos(double a) { return std::cos(a); }
static double fnExp(double a) { return std::exp(a); }
static double fnFloor(double a) { return std::floor(a); }
static double fnAbs(double a) { return std::fabs(a); }
static double fnCent(double c) { return std::pow(2.0, c / 1200.0); }

std::unique_ptr<Node> parseExpr(Lexer& lex);

std::unique_ptr<Node> parsePrimary(Lexer& lex) {
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
        if (lex.curTok != TOK_RPAREN) throw std::runtime_error("Expected ')'");
        lex.next();

        if (id == "W1") return std::make_unique<WaveCallNode>(1, std::move(a1));
        if (id == "W2") return std::make_unique<WaveCallNode>(2, std::move(a1));
        if (id == "W3") return std::make_unique<WaveCallNode>(3, std::move(a1));
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

        throw std::runtime_error("Unknown function: " + id);
    }
    if (lex.curTok == TOK_LPAREN) {
        lex.next();
        auto n = parseExpr(lex);
        if (lex.curTok != TOK_RPAREN) throw std::runtime_error("Expected ')'");
        lex.next();
        return n;
    }
    throw std::runtime_error("Syntax error in formula");
}

std::unique_ptr<Node> parseUnary(Lexer& lex) {
    if (lex.curTok == TOK_MINUS) { lex.next(); return std::make_unique<UnaryNode>(parseUnary(lex)); }
    if (lex.curTok == TOK_PLUS) { lex.next(); return parseUnary(lex); }
    return parsePrimary(lex);
}

std::unique_ptr<Node> parseMulDiv(Lexer& lex) {
    auto l = parseUnary(lex);
    while (lex.curTok == TOK_MUL || lex.curTok == TOK_DIV) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseUnary(lex), op);
    }
    return l;
}

std::unique_ptr<Node> parseAddSub(Lexer& lex) {
    auto l = parseMulDiv(lex);
    while (lex.curTok == TOK_PLUS || lex.curTok == TOK_MINUS) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseMulDiv(lex), op);
    }
    return l;
}

std::unique_ptr<Node> parseRel(Lexer& lex) {
    auto l = parseAddSub(lex);
    while (lex.curTok == TOK_LT || lex.curTok == TOK_GT || lex.curTok == TOK_LTE || lex.curTok == TOK_GTE) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseAddSub(lex), op);
    }
    return l;
}

std::unique_ptr<Node> parseEq(Lexer& lex) {
    auto l = parseRel(lex);
    while (lex.curTok == TOK_EQ || lex.curTok == TOK_NEQ) {
        Token op = lex.curTok; lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseRel(lex), op);
    }
    return l;
}

std::unique_ptr<Node> parseAnd(Lexer& lex) {
    auto l = parseEq(lex);
    while (lex.curTok == TOK_AND) {
        lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseEq(lex), TOK_AND);
    }
    return l;
}

std::unique_ptr<Node> parseOr(Lexer& lex) {
    auto l = parseAnd(lex);
    while (lex.curTok == TOK_OR) {
        lex.next();
        l = std::make_unique<BinOpNode>(std::move(l), parseAnd(lex), TOK_OR);
    }
    return l;
}

std::unique_ptr<Node> parseExpr(Lexer& lex) {
    auto cond = parseOr(lex);
    if (lex.curTok == TOK_QUESTION) {
        lex.next();
        auto tBranch = parseExpr(lex);
        if (lex.curTok != TOK_COLON) throw std::runtime_error("Expected ':' in ternary");
        lex.next();
        auto fBranch = parseExpr(lex);
        return std::make_unique<TernaryCondNode>(std::move(cond), std::move(tBranch), std::move(fBranch));
    }
    return cond;
}

std::unique_ptr<Node> parse(const std::string& str, int* outIntegratorCount) {
    if (str.empty()) return std::make_unique<NumNode>(0.0);
    Lexer lex(str.c_str());
    auto root = parseExpr(lex);
    if (outIntegratorCount) *outIntegratorCount = lex.integratorCount;
    return root;
}

} // namespace XpressiveParser
