#include <core/tokens.hpp>

#include <cctype>

namespace wok {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

}  // namespace

std::string serverKey(std::string_view host, uint16_t port) {
    std::string k;
    for (char c : host) k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return k + ":" + std::to_string(port);
}

TokenFile parseTokens(std::string_view text) {
    TokenFile f;
    while (!text.empty()) {
        size_t eol = text.find('\n');
        std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.rfind('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = trim(line.substr(0, eq)), hex = trim(line.substr(eq + 1));
        if (key.empty() || hex.size() != 64) continue;
        AuthToken t{};
        bool ok = true;
        for (size_t i = 0; i < t.size() && ok; ++i) {
            int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
            ok = hi >= 0 && lo >= 0;
            t[i] = static_cast<uint8_t>(hi * 16 + lo);
        }
        if (ok) f[std::string(key)] = t;
    }
    return f;
}

std::string serializeTokens(const TokenFile& f) {
    static const char* digits = "0123456789abcdef";
    std::string out = "# World of Kenshi - one secret per server. Never share this file.\n";
    for (auto& [key, t] : f) {
        out += key + "=";
        for (uint8_t b : t) {
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 15]);
        }
        out.push_back('\n');
    }
    return out;
}

}  // namespace wok
