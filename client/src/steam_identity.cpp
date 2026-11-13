#include "steam_identity.hpp"

#include "log.hpp"

#include <windows.h>

namespace wok {

uint64_t localSteamId() {
    HMODULE steam = GetModuleHandleA("steam_api64.dll");
    if (!steam) {
        log("steam: steam_api64.dll not loaded");
        return 0;
    }
    using GetHandleFn = int (*)();
    using SteamClientFn = void* (*)();
    using GetUserFn = void* (*)(void* client, int hUser, int hPipe, const char* version);
    using GetSteamIdFn = uint64_t (*)(void* user);
    auto getUser = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
    auto getPipe = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamPipe"));
    auto client = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    auto clientGetUser = reinterpret_cast<GetUserFn>(GetProcAddress(steam, "SteamAPI_ISteamClient_GetISteamUser"));
    auto userGetId = reinterpret_cast<GetSteamIdFn>(GetProcAddress(steam, "SteamAPI_ISteamUser_GetSteamID"));
    if (!getUser || !getPipe || !client || !clientGetUser || !userGetId) {
        log("steam: flat API exports missing");
        return 0;
    }
    void* c = client();
    void* user = c ? clientGetUser(c, getUser(), getPipe(), "SteamUser019") : nullptr;
    uint64_t id = user ? userGetId(user) : 0;
    log("steam: SteamID %s", id ? "found" : "unavailable");
    return id;
}

}  // namespace wok
