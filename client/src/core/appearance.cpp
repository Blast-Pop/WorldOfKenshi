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

}  // namespace wok
