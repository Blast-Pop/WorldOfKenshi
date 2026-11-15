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
        } else if (key == "server") {
            size_t comma = value.find(',');
            if (comma == std::string_view::npos) continue;
            std::string_view n = trim(value.substr(0, comma)), a = trim(value.substr(comma + 1));
            if (!n.empty() && parseAddress(a)) cfg.servers.push_back({std::string(n), std::string(a)});
        } else if (key == "master") {
            cfg.masterUrl = std::string(value);
        } else if (key == "chat_fade" || key == "name_range") {
            float v = 0;
            if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{}) {
                if (key == "chat_fade" && v >= 5.0f && v <= 120.0f) cfg.chatFadeSec = v;
                if (key == "name_range" && v >= 50.0f && v <= 2000.0f) cfg.nameRange = v;
            }
        } else if (key == "show_names") {
            cfg.showNames = value != "0";
        } else if (key == "chat_size") {
            size_t comma = value.find(',');
            int w = 0, h = 0;
            if (comma != std::string_view::npos &&
                std::from_chars(value.data(), value.data() + comma, w).ec == std::errc{} &&
                std::from_chars(value.data() + comma + 1, value.data() + value.size(), h).ec == std::errc{} &&
                w >= 100 && h >= 50 && w <= 10000 && h <= 10000) {
                cfg.chatWidth = w;
                cfg.chatHeight = h;
            }
        } else if (key == "chat_opacity") {
            float v = 0;
            auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
            if (ec == std::errc{} && v >= 20.0f && v <= 100.0f) cfg.chatOpacity = v;
        } else if (key == "name") {
            cfg.name = std::string(value.substr(0, 31));  // Hello::name is 32 bytes incl. NUL
        }
    }
    return cfg;
}

std::string serializeConfig(const ClientConfig& cfg) {
    std::string out = "# World of Kenshi client configuration\n"
                      "host=" + cfg.host + "\nport=" + std::to_string(cfg.port) + "\nname=" + cfg.name + "\n"
                      "# Servers shown in the Multiplayer window: server=Name,host:port\n";
    for (auto& s : cfg.servers) out += "server=" + s.name + "," + s.address + "\n";
    if (cfg.masterUrl != kDefaultMasterUrl) out += "master=" + cfg.masterUrl + "\n";
    out += "chat_opacity=" + std::to_string(static_cast<int>(cfg.chatOpacity + 0.5f)) + "\n";
    out += "chat_size=" + std::to_string(cfg.chatWidth) + "," + std::to_string(cfg.chatHeight) + "\n";
    out += "chat_fade=" + std::to_string(static_cast<int>(cfg.chatFadeSec + 0.5f)) + "\n";
    out += std::string("show_names=") + (cfg.showNames ? "1" : "0") + "\n";
    out += "name_range=" + std::to_string(static_cast<int>(cfg.nameRange + 0.5f)) + "\n";
    return out;
}

std::vector<ServerEntry> parseServerList(std::string_view text) {
    std::vector<ServerEntry> out;
    while (!text.empty() && out.size() < 500) {
        size_t eol = text.find('\n');
        std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (line.find(':') == std::string_view::npos || !parseAddress(line)) continue;
        std::string address(line);
        bool seen = false;
        for (auto& e : out) seen = seen || e.address == address;
        if (!seen) out.push_back({"", address});
    }
    return out;
}

std::optional<Address> parseAddress(std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;
    Address a{std::string(text), WOK_DEFAULT_PORT};
    size_t colon = text.rfind(':');
    if (colon != std::string_view::npos) {
        std::string_view portText = text.substr(colon + 1);
        unsigned v = 0;
        auto [p, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), v);
        if (portText.empty() || ec != std::errc{} || p != portText.data() + portText.size() || v == 0 || v > 65535)
            return std::nullopt;
        a.host = std::string(text.substr(0, colon));
        a.port = static_cast<uint16_t>(v);
    }
    if (a.host.empty() || a.host.find_first_of(" \t") != std::string::npos) return std::nullopt;
    return a;
}

}  // namespace wok
