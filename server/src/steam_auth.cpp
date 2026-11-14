#include <wok/steam_auth.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wok {

std::mutex& verdictMutex();
std::map<uint64_t, int>& verdicts();

namespace {

constexpr uint32_t kKenshiAppId = 233860;
constexpr int kServerModeAuthentication = 2;   // authenticate users, no VAC, not listed
constexpr uint16_t kQueryPortShared = 0xFFFF;  // no separate query socket

void* openLibrary(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

using InitFn = bool (*)(uint32_t, uint16_t, uint16_t, uint16_t, int, const char*);
using HandleFn = int (*)();
using CreateFn = void* (*)(const char*);
using GetGsFn = void* (*)(void*, int, int, const char*);
using GsVoidFn = void (*)(void*);
using GsBoolFn = bool (*)(void*);
using GsSetBoolFn = void (*)(void*, bool);
using BeginFn = int (*)(void*, const void*, int, uint64_t);
using LicenceFn = int (*)(void*, uint64_t, uint32_t);
using EndFn = void (*)(void*, uint64_t);
using RunFn = void (*)();

// ValidateAuthTicketResponse_t (Steamworks): Steam's verdict on a BeginAuthSession ticket.
constexpr int kValidateAuthTicketResponse = 143;  // k_iSteamUserCallbacks (100) + 43
#ifdef _WIN32
constexpr size_t kResponseOffset = 8;   // CSteamID, then the enum (pack 8)
constexpr int kResponseSize = 24;
#else
constexpr size_t kResponseOffset = 8;   // pack 4 on Linux: CSteamID (8), enum (4), CSteamID (8)
constexpr int kResponseSize = 20;
#endif

// Same declarations as the Steamworks CCallbackBase, so the compiler lays out the same vtable the
// Steam API library calls into.
class CallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool ioFailure, uint64_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;

protected:
    uint8_t flags_ = 0x02;  // k_ECallbackFlagsGameServer: deliver from the game-server pipe
    int callbackId_ = kValidateAuthTicketResponse;
};

class AuthResponseCallback : public CallbackBase {
public:
    void Run(void* param) override {
        uint64_t steamId = 0;
        int response = -1;
        std::memcpy(&steamId, param, 8);
        std::memcpy(&response, static_cast<char*>(param) + kResponseOffset, 4);
        std::lock_guard lock(verdictMutex());
        verdicts()[steamId] = response;
    }
    void Run(void* param, bool, uint64_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return kResponseSize; }
};

AuthResponseCallback g_authResponse;

}  // namespace

std::mutex& verdictMutex() {
    static std::mutex m;
    return m;
}
std::map<uint64_t, int>& verdicts() {
    static std::map<uint64_t, int> v;  // SteamID -> EAuthSessionResponse (0 = OK)
    return v;
}

SteamAuth::~SteamAuth() {
    if (gs_ && shutdownFn_) reinterpret_cast<RunFn>(shutdownFn_)();
}

bool SteamAuth::start(const std::string& library, uint16_t gamePort) {
    // The Steam API reads the app id from the environment or a steam_appid.txt in the working dir.
    setEnv("SteamAppId", "233860");
    setEnv("SteamGameId", "233860");
    std::ofstream("steam_appid.txt") << kKenshiAppId;
    lib_ = openLibrary(library);
    if (!lib_) {
        std::printf("steam: cannot load %s (browser login only)\n", library.c_str());
        return false;
    }
    auto init = reinterpret_cast<InitFn>(symbol(lib_, "SteamInternal_GameServer_Init"));
    auto gsUser = reinterpret_cast<HandleFn>(symbol(lib_, "SteamGameServer_GetHSteamUser"));
    auto gsPipe = reinterpret_cast<HandleFn>(symbol(lib_, "SteamGameServer_GetHSteamPipe"));
    auto create = reinterpret_cast<CreateFn>(symbol(lib_, "SteamInternal_CreateInterface"));
    auto getGs = reinterpret_cast<GetGsFn>(symbol(lib_, "SteamAPI_ISteamClient_GetISteamGameServer"));
    auto logOn = reinterpret_cast<GsVoidFn>(symbol(lib_, "SteamAPI_ISteamGameServer_LogOnAnonymous"));
    auto loggedOn = reinterpret_cast<GsBoolFn>(symbol(lib_, "SteamAPI_ISteamGameServer_BLoggedOn"));
    auto dedicated = reinterpret_cast<GsSetBoolFn>(symbol(lib_, "SteamAPI_ISteamGameServer_SetDedicatedServer"));
    beginFn_ = symbol(lib_, "SteamAPI_ISteamGameServer_BeginAuthSession");
    licenceFn_ = symbol(lib_, "SteamAPI_ISteamGameServer_UserHasLicenseForApp");
    endFn_ = symbol(lib_, "SteamAPI_ISteamGameServer_EndAuthSession");
    runFn_ = symbol(lib_, "SteamGameServer_RunCallbacks");
    shutdownFn_ = symbol(lib_, "SteamGameServer_Shutdown");
    // Recent Steamworks SDKs (the Linux libsteam_api.so) replaced the init call and the interface version.
    using InitV2Fn = int (*)(uint32_t, uint16_t, uint16_t, int, const char*, const char*, char*);
    using FindGsFn = void* (*)(int, const char*);
    auto initV2 = reinterpret_cast<InitV2Fn>(symbol(lib_, "SteamInternal_GameServer_Init_V2"));
    auto findGs = reinterpret_cast<FindGsFn>(symbol(lib_, "SteamInternal_FindOrCreateGameServerInterface"));
    if ((!init && !initV2) || !gsUser || !logOn || !loggedOn || !beginFn_ || !licenceFn_ || !endFn_ || !runFn_) {
        std::printf("steam: the Steam API library lacks game-server functions (browser login only)\n");
        return false;
    }
    void* gs = nullptr;
    if (initV2 && findGs) {
        char error[1024] = {};
        int r = initV2(0, gamePort, kQueryPortShared, kServerModeAuthentication, "1.0.68", "SteamGameServer015\0", error);
        if (r != 0) {
            std::printf("steam: game-server init failed (%d: %s) (browser login only)\n", r, error);
            return false;
        }
        gs = findGs(gsUser(), "SteamGameServer015");
    } else {
        if (!gsPipe || !create || !getGs || !init(0, 0, gamePort, kQueryPortShared, kServerModeAuthentication, "1.0.68")) {
            std::printf("steam: game-server init failed (browser login only)\n");
            return false;
        }
        void* client = create("SteamClient017");
        gs = client ? getGs(client, gsUser(), gsPipe(), "SteamGameServer012") : nullptr;
    }
    if (!gs) {
        std::printf("steam: no game-server interface (browser login only)\n");
        return false;
    }
    if (dedicated) dedicated(gs, true);
    logOn(gs);
    for (int i = 0; i < 150 && !loggedOn(gs); ++i) {
        reinterpret_cast<RunFn>(runFn_)();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!loggedOn(gs)) {
        std::printf("steam: anonymous game-server logon failed (browser login only)\n");
        return false;
    }
    using RegisterFn = void (*)(void*, int);
    if (auto reg = reinterpret_cast<RegisterFn>(symbol(lib_, "SteamAPI_RegisterCallback"))) {
        reg(&g_authResponse, kValidateAuthTicketResponse);
    } else {
        std::printf("steam: cannot listen to ticket verdicts (browser login only)\n");
        return false;
    }
    gs_ = gs;
    std::printf("steam: logged on, players are checked with Steam (no browser login)\n");
    return true;
}

bool SteamAuth::begin(uint64_t steamId, const std::string& ticket) {
    if (!gs_ || ticket.empty() || ticket.size() > 1024) return false;
    {
        std::lock_guard lock(verdictMutex());
        verdicts().erase(steamId);  // a new check: wait for Steam's verdict on this ticket
    }
    return reinterpret_cast<BeginFn>(beginFn_)(gs_, ticket.data(), static_cast<int>(ticket.size()), steamId) == 0;
}

int SteamAuth::licence(uint64_t steamId) {
    if (!gs_) return 2;
    int verdict = -1;
    {
        std::lock_guard lock(verdictMutex());
        if (auto it = verdicts().find(steamId); it != verdicts().end()) verdict = it->second;
    }
    // Only Steam's verdict proves the ticket is valid and unused (not cancelled, not replayed).
    if (verdict < 0) return 2;
    if (verdict != 0) {
        std::printf("[steam] %llu refused by Steam (auth session response %d)\n", static_cast<unsigned long long>(steamId), verdict);
        return 1;
    }
    return reinterpret_cast<LicenceFn>(licenceFn_)(gs_, steamId, kKenshiAppId);
}

void SteamAuth::end(uint64_t steamId) {
    if (gs_) reinterpret_cast<EndFn>(endFn_)(gs_, steamId);
}

void SteamAuth::runCallbacks() {
    if (gs_) reinterpret_cast<RunFn>(runFn_)();
}

}  // namespace wok
