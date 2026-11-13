#pragma once
// Master server list: game servers announce themselves (HTTP, every minute); players' clients
// download the list for their server browser. Any server can act as master (--master).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace wok {

inline constexpr double kMasterEntryTtlSec = 180.0;   // a server disappears 3 min after its last announce
inline constexpr size_t kMasterMaxEntries = 500;

class MasterList {
public:
    // host as seen by the master (the announcer's IP), port of its game socket. False if invalid/full.
    bool announce(const std::string& host, uint16_t port, double nowSec);
    // "host:port\n" per live server, sorted.
    std::string listText(double nowSec);

private:
    void expire(double nowSec);
    std::map<std::pair<std::string, uint16_t>, double> lastSeen_;
};

}  // namespace wok
