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

// What a server says about itself when it announces (optional: older servers send only the port).
struct MasterDetails {
    std::string name;
    uint32_t players = 0, maxPlayers = 0;
    bool known = false;  // the announce carried these details
};

class MasterList {
public:
    // host as seen by the master (the announcer's IP), port of its game socket. False if invalid/full.
    bool announce(const std::string& host, uint16_t port, double nowSec, const MasterDetails& details = {});
    // "host:port\n" per live server, sorted.
    std::string listText(double nowSec);
    // The same servers as JSON, with what they said about themselves:
    // [{"address":"host:port","name":"...","players":1,"maxPlayers":250}, ...]
    std::string listJson(double nowSec);

private:
    struct Entry {
        double lastSeen = 0;
        MasterDetails details;
    };
    void expire(double nowSec);
    std::map<std::pair<std::string, uint16_t>, Entry> entries_;
};

}  // namespace wok
