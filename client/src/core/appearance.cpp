#include <core/appearance.hpp>

#include <string_view>

namespace wok {

std::vector<uint8_t> encodeAppearance(const Appearance& a) {
    std::string text;
    for (auto& [k, v] : a) {  // std::map iterates in key order: stable, sorted output
        std::string line = k + "=" + v + "\n";
        if (text.size() + line.size() > kMaxAppearanceBytes) break;
        text += line;
    }
    return std::vector<uint8_t>(text.begin(), text.end());
}

Appearance decodeAppearance(const std::vector<uint8_t>& blob) {
    Appearance a;
    std::string_view text(reinterpret_cast<const char*>(blob.data()), blob.size());
    while (!text.empty()) {
        size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0 || eq + 1 == line.size()) continue;
        a[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 1));
    }
    return a;
}

namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}  // namespace

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i + 1] << 8;
        if (i + 2 < data.size()) n |= data[i + 2];
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += i + 1 < data.size() ? kB64[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kB64[n & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(std::string_view text) {
    std::vector<uint8_t> out;
    if (text.size() % 4 != 0) return {};
    for (size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            char c = text[i + k];
            if (c == '=' && i + 4 == text.size() && k >= 2) {
                v[k] = 0;
                ++pad;
            } else if (pad || (v[k] = b64Value(c)) < 0) {
                return {};
            }
        }
        uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        out.push_back(static_cast<uint8_t>(n >> 16));
        if (pad < 2) out.push_back(static_cast<uint8_t>(n >> 8));
        if (pad < 1) out.push_back(static_cast<uint8_t>(n));
    }
    return out;
}

}  // namespace wok
