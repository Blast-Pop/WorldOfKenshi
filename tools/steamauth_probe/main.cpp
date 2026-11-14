// Prototype (Windows): can a dedicated server check a player's Steam session ticket for Kenshi's
// app id with an anonymous Steam game-server logon? Uses the steam_api64.dll shipped with Kenshi.
//   steamauth_probe <path to Kenshi> <steam_ticket.txt written by the client's Ctrl+Shift+F4>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<uint8_t> fromHex(const std::string& s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

template <class F>
F load(HMODULE m, const char* name) {
    auto f = reinterpret_cast<F>(GetProcAddress(m, name));
    if (!f) std::printf("missing export %s\n", name);
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: steamauth_probe <Kenshi dir> <steam_ticket.txt>\n");
        return 2;
    }
    std::ifstream in(argv[2]);
    std::string idText, hex;
    in >> idText >> hex;
    uint64_t steamId = std::stoull(idText);
    auto ticket = fromHex(hex);
    std::printf("ticket: %zu bytes for SteamID %llu\n", ticket.size(), static_cast<unsigned long long>(steamId));

    // The game server identifies itself as Kenshi (app 233860).
    std::ofstream("steam_appid.txt") << "233860";
    SetEnvironmentVariableA("SteamAppId", "233860");
    SetEnvironmentVariableA("SteamGameId", "233860");
    std::string dll = std::string(argv[1]) + "\\steam_api64.dll";
    HMODULE steam = LoadLibraryA(dll.c_str());
    if (!steam) {
        std::printf("cannot load %s\n", dll.c_str());
        return 1;
    }
    using InitFn = bool (*)(uint32_t ip, uint16_t steamPort, uint16_t gamePort, uint16_t queryPort, int mode, const char* version);
    using HandleFn = int (*)();
    using CreateFn = void* (*)(const char*);
    using GetGsFn = void* (*)(void* client, int user, int pipe, const char* version);
    using VoidFn = void (*)(void*);
    using BoolFn = bool (*)(void*);
    using BeginFn = int (*)(void* gs, const void* ticket, int size, uint64_t steamId);
    using LicenseFn = int (*)(void* gs, uint64_t steamId, uint32_t appId);
    using RunFn = void (*)();
    auto init = load<InitFn>(steam, "SteamInternal_GameServer_Init");
    auto gsUser = load<HandleFn>(steam, "SteamGameServer_GetHSteamUser");
    auto gsPipe = load<HandleFn>(steam, "SteamGameServer_GetHSteamPipe");
    auto create = load<CreateFn>(steam, "SteamInternal_CreateInterface");
    auto getGs = load<GetGsFn>(steam, "SteamAPI_ISteamClient_GetISteamGameServer");
    auto logOnAnon = load<VoidFn>(steam, "SteamAPI_ISteamGameServer_LogOnAnonymous");
    auto loggedOn = load<BoolFn>(steam, "SteamAPI_ISteamGameServer_BLoggedOn");
    auto setDedicated = reinterpret_cast<void (*)(void*, bool)>(GetProcAddress(steam, "SteamAPI_ISteamGameServer_SetDedicatedServer"));
    auto begin = load<BeginFn>(steam, "SteamAPI_ISteamGameServer_BeginAuthSession");
    auto license = load<LicenseFn>(steam, "SteamAPI_ISteamGameServer_UserHasLicenseForApp");
    auto run = load<RunFn>(steam, "SteamGameServer_RunCallbacks");
    if (!init || !gsUser || !gsPipe || !create || !getGs || !logOnAnon || !loggedOn || !begin || !license || !run) return 1;

    // eServerModeAuthentication = 2: authenticate users, no VAC/listing.
    if (!init(0, 27815, 27816, 27817, 2, "1.0.68")) {
        std::printf("SteamGameServer init FAILED\n");
        return 1;
    }
    void* client = create("SteamClient017");
    void* gs = client ? getGs(client, gsUser(), gsPipe(), "SteamGameServer012") : nullptr;
    std::printf("game server interface: %s\n", gs ? "ok" : "MISSING");
    if (!gs) return 1;
    if (setDedicated) setDedicated(gs, true);
    logOnAnon(gs);
    for (int i = 0; i < 150 && !loggedOn(gs); ++i) {
        run();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("anonymous logon: %s\n", loggedOn(gs) ? "OK" : "FAILED (timeout)");
    int r = begin(gs, ticket.data(), static_cast<int>(ticket.size()), steamId);
    static const char* kResults[] = {"OK", "InvalidTicket", "DuplicateRequest", "InvalidVersion", "GameMismatch", "ExpiredTicket"};
    std::printf("BeginAuthSession: %d (%s)\n", r, r >= 0 && r < 6 ? kResults[r] : "?");
    for (int i = 0; i < 50; ++i) {  // let Steam answer the validation
        run();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    int lic = license(gs, steamId, 233860);
    std::printf("UserHasLicenseForApp(233860): %d (0 = has license, 1 = no, 2 = not authenticated)\n", lic);
    return 0;
}
