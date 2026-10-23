#ifndef XPRESSIVE_PARSER_H
#define XPRESSIVE_PARSER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace XpressiveParser {

class Node;

struct Env {
    double t = 0.0;
    double tempo = 120.0;
    double srate = 44100.0;
    double f = 440.0;
    double rel = 0.0;
    double trel = 0.0;
    double A1 = 1.0, A2 = 1.0, A3 = 1.0;
    double lastOut = 0.0; // Supports last(1)

    const Node* w1 = nullptr;
    const Node* w2 = nullptr;
    const Node* w3 = nullptr;

    const std::vector<float>* w1Sample = nullptr;
    const std::vector<float>* w2Sample = nullptr;
    const std::vector<float>* w3Sample = nullptr;

    double* intState = nullptr;
    int numIntegrators = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual double eval(Env& env) const = 0;
};

std::unique_ptr<Node> parse(const std::string& str, int* outIntegratorCount = nullptr);

} // namespace XpressiveParser

#endif // XPRESSIVE_PARSER_H
