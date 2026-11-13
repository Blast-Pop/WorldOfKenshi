#include <core/config.hpp>

#include <charconv>

namespace wok {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

}  // namespace

ClientConfig parseConfig(std::string_view text) {
    ClientConfig cfg;
    while (!text.empty()) {
        size_t eol = text.find('\n');
        std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = trim(line.substr(0, eq));
        std::string_view value = trim(line.substr(eq + 1));
        if (value.empty()) continue;
        if (key == "host") {
            cfg.host = std::string(value);
        } else if (key == "port") {
            unsigned v = 0;
            auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
            if (ec == std::errc{} && p == value.data() + value.size() && v > 0 && v <= 65535)
                cfg.port = static_cast<uint16_t>(v);
        } else if (key == "name") {
            cfg.name = std::string(value.substr(0, 31));  // Hello::name is 32 bytes incl. NUL
        }
    }
    return cfg;
}

std::string serializeConfig(const ClientConfig& cfg) {
    return "# World of Kenshi client configuration\n"
           "host=" + cfg.host + "\nport=" + std::to_string(cfg.port) + "\nname=" + cfg.name + "\n";
}

}  // namespace wok
