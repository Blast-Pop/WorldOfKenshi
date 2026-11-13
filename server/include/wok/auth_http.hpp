#pragma once
// Small HTTP endpoint of the dedicated server for Steam OpenID (docs/m2-auth.md):
//   GET /auth/start?nonce=N   -> redirects the player's browser to Steam's login page
//   GET /auth/callback?...    -> verifies the answer with Steam, then reports (nonce, SteamID)
// Runs on its own thread; the game loop drains verified logins with poll().

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace httplib {
class Server;
}

namespace wok {

class MasterList;

class AuthHttp {
public:
    // publicUrl: how players' browsers reach this endpoint, e.g. "http://45.32.1.2:27801" (no trailing slash).
    AuthHttp(uint16_t port, std::string publicUrl);
    ~AuthHttp();
    bool start();
    std::vector<std::pair<std::string, uint64_t>> poll();  // verified (nonce, steamId) since last call
    std::string challengeUrlBase() const;                  // ".../auth/start?nonce="
    // Also serve the master server list (call before start()):
    //   GET  /master/servers         -> "host:port" lines of live servers
    //   POST /master/announce port=N -> registers the caller's IP with that game port
    void enableMaster();

private:
    uint16_t port_;
    std::string publicUrl_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::mutex mutex_;
    std::vector<std::pair<std::string, uint64_t>> verified_;
    std::unique_ptr<MasterList> master_;  // guarded by mutex_
};

}  // namespace wok
