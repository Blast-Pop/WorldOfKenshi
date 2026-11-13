#include <wok/master_list.hpp>

#include <cctype>
#include <iterator>

namespace wok {

namespace {
bool validHost(const std::string& h) {
    if (h.empty() || h.size() > 253) return false;
    for (char c : h)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) return false;
    return true;
}
}  // namespace

void MasterList::expire(double nowSec) {
    for (auto it = lastSeen_.begin(); it != lastSeen_.end();)
        it = nowSec - it->second > kMasterEntryTtlSec ? lastSeen_.erase(it) : std::next(it);
}

bool MasterList::announce(const std::string& host, uint16_t port, double nowSec) {
    if (port == 0 || !validHost(host)) return false;
    expire(nowSec);
    auto key = std::make_pair(host, port);
    if (!lastSeen_.contains(key) && lastSeen_.size() >= kMasterMaxEntries) return false;
    lastSeen_[key] = nowSec;
    return true;
}

std::string MasterList::listText(double nowSec) {
    expire(nowSec);
    std::string out;
    for (auto& [key, seen] : lastSeen_) out += key.first + ":" + std::to_string(key.second) + "\n";
    return out;
}

}  // namespace wok
