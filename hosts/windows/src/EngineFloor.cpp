#include "EngineFloor.hpp"

#include <vector>

namespace {

std::vector<long long> parts(const std::string& v) {
    std::vector<long long> out;
    size_t i = 0;
    while (i <= v.size()) {
        size_t dot = v.find('.', i);
        std::string tok = v.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
        long long n = 0;
        bool numeric = !tok.empty();
        for (char c : tok) {
            if (c < '0' || c > '9') { numeric = false; break; }
            n = n * 10 + (c - '0');
        }
        out.push_back(numeric ? n : 0);
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    return out;
}

}  // namespace

namespace EngineFloor {

int compare(const std::string& a, const std::string& b) {
    auto pa = parts(a), pb = parts(b);
    size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < n; i++) {
        long long xa = i < pa.size() ? pa[i] : 0;
        long long xb = i < pb.size() ? pb[i] : 0;
        if (xa != xb) return xa < xb ? -1 : 1;
    }
    return 0;
}

bool isBelowFloor(const std::string& detected) { return compare(detected, Required) < 0; }

}  // namespace EngineFloor
