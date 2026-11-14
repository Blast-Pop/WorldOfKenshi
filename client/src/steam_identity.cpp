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

namespace {
uint32_t g_ticketHandle = 0;
void* steamUser() {
    HMODULE steam = GetModuleHandleA("steam_api64.dll");
    if (!steam) return nullptr;
    using GetHandleFn = int (*)();
    using SteamClientFn = void* (*)();
    using GetUserFn = void* (*)(void* client, int hUser, int hPipe, const char* version);
    auto getUser = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
    auto getPipe = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamPipe"));
    auto client = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    auto clientGetUser = reinterpret_cast<GetUserFn>(GetProcAddress(steam, "SteamAPI_ISteamClient_GetISteamUser"));
    if (!getUser || !getPipe || !client || !clientGetUser) return nullptr;
    void* c = client();
    return c ? clientGetUser(c, getUser(), getPipe(), "SteamUser019") : nullptr;
}
}  // namespace

void cancelSteamAuthTicket() {
    HMODULE steam = GetModuleHandleA("steam_api64.dll");
    auto cancel = steam ? reinterpret_cast<void (*)(void*, uint32_t)>(GetProcAddress(steam, "SteamAPI_ISteamUser_CancelAuthTicket"))
                        : nullptr;
    void* user = steamUser();
    if (!cancel || !user || !g_ticketHandle) return;
    cancel(user, g_ticketHandle);
    log("steam: session ticket cancelled");
    g_ticketHandle = 0;
}

std::vector<uint8_t> steamAuthTicket() {
    HMODULE steam = GetModuleHandleA("steam_api64.dll");
    if (!steam) return {};
    using GetHandleFn = int (*)();
    using SteamClientFn = void* (*)();
    using GetUserFn = void* (*)(void* client, int hUser, int hPipe, const char* version);
    using GetTicketFn = uint32_t (*)(void* user, void* buffer, int size, uint32_t* written);
    auto getUser = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
    auto getPipe = reinterpret_cast<GetHandleFn>(GetProcAddress(steam, "SteamAPI_GetHSteamPipe"));
    auto client = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    auto clientGetUser = reinterpret_cast<GetUserFn>(GetProcAddress(steam, "SteamAPI_ISteamClient_GetISteamUser"));
    auto getTicket = reinterpret_cast<GetTicketFn>(GetProcAddress(steam, "SteamAPI_ISteamUser_GetAuthSessionTicket"));
    if (!getUser || !getPipe || !client || !clientGetUser || !getTicket) return {};
    void* c = client();
    void* user = c ? clientGetUser(c, getUser(), getPipe(), "SteamUser019") : nullptr;
    if (!user) return {};
    std::vector<uint8_t> ticket(1024);
    uint32_t written = 0;
    uint32_t handle = getTicket(user, ticket.data(), static_cast<int>(ticket.size()), &written);
    ticket.resize(handle && written <= 1024 ? written : 0);
    if (handle) g_ticketHandle = handle;
    log("steam: session ticket %s (%u bytes)", ticket.empty() ? "unavailable" : "obtained", written);
    return ticket;
}

}  // namespace wok
