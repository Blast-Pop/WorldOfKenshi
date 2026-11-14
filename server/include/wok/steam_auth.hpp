#pragma once
// Steam session ticket checks for the dedicated server (docs/m2-auth.md): an anonymous Steamworks
// game-server logon with Kenshi's app id, then BeginAuthSession per player. The Steam API library
// (steam_api64.dll on Windows, libsteam_api.so on Linux) is loaded at run time and is not part of
// this repository; without it the server keeps the browser (OpenID) login.

#include <cstdint>
#include <string>

namespace wok {

class SteamAuth {
public:
    ~SteamAuth();
    // Loads the Steam API from `library` and logs on anonymously (waits up to ~15 s). False if
    // Steam is unavailable; the reason is printed.
    bool start(const std::string& library, uint16_t gamePort);
    bool ready() const { return gs_ != nullptr; }

    bool begin(uint64_t steamId, const std::string& ticket);  // ticket accepted for checking
    int licence(uint64_t steamId);                            // 0 owns Kenshi, 1 does not, 2 checking
    void end(uint64_t steamId);
    void runCallbacks();  // call every server tick

private:
    void* lib_ = nullptr;
    void* gs_ = nullptr;  // ISteamGameServer
    void* beginFn_ = nullptr;
    void* licenceFn_ = nullptr;
    void* endFn_ = nullptr;
    void* runFn_ = nullptr;
    void* shutdownFn_ = nullptr;
};

}  // namespace wok
