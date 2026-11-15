#include <wok/master_list.hpp>

#include <cctype>
#include <cstdio>
#include <iterator>

namespace wok {

namespace {
bool validHost(const std::string& h) {
    if (h.empty() || h.size() > 253) return false;
    for (char c : h)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) return false;
    return true;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}}  // namespace

void MasterList::expire(double nowSec) {
    for (auto it = entries_.begin(); it != entries_.end();)
        it = nowSec - it->second.lastSeen > kMasterEntryTtlSec ? entries_.erase(it) : std::next(it);
}

bool MasterList::announce(const std::string& host, uint16_t port, double nowSec, const MasterDetails& details) {
    if (port == 0 || !validHost(host)) return false;
    expire(nowSec);
    auto key = std::make_pair(host, port);
    if (!entries_.contains(key) && entries_.size() >= kMasterMaxEntries) return false;
    Entry& e = entries_[key];
    e.lastSeen = nowSec;
    if (details.known) {
        e.details = details;
        e.details.name = details.name.substr(0, 64);
    }
    return true;
}

std::string MasterList::listText(double nowSec) {
    expire(nowSec);
    std::string out;
    for (auto& [key, e] : entries_) out += key.first + ":" + std::to_string(key.second) + "\n";
    return out;
}

std::string MasterList::listJson(double nowSec) {
    expire(nowSec);
    std::string out = "[";
    for (auto& [key, e] : entries_) {
        if (out.size() > 1) out += ",";
        out += "{\"address\":" + jsonString(key.first + ":" + std::to_string(key.second));
        if (e.details.known)
            out += ",\"name\":" + jsonString(e.details.name) + ",\"players\":" + std::to_string(e.details.players) +
                   ",\"maxPlayers\":" + std::to_string(e.details.maxPlayers);
        out += "}";
    }
    return out + "]\n";
}

}  // namespace wok
