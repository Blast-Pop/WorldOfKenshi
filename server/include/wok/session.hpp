#pragma once
// Pure session logic of the dedicated server: no sockets, fully unit-testable.
// The transport feeds events in; the manager returns the messages to send.
// With a CharacterStore, players authenticate (token, or Steam OpenID on a first visit - see
// docs/m2-auth.md) and their characters live on the server.

#include <wok/protocol.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wok {

class CharacterStore;

inline constexpr double kPositionSaveIntervalSec = 10.0;
// Test bots (wok_testclient) use fake ids below this value; real SteamID64s start at 76561197960265728.
inline constexpr uint64_t kMaxTestBotSteamId = 1000000;

struct Outgoing {
    ConnId to;
    Message msg;
};

struct SessionConfig {
    uint32_t maxPlayers = 250;
    double timeoutSec = 10.0;
    uint32_t maxBadPackets = 3;
    std::unordered_set<uint64_t> bannedSteamIds;

    // Server-side characters. Without a store, any Hello is accepted (no authentication).
    CharacterStore* store = nullptr;
    std::string authUrlBase;                  // e.g. "http://host:27801/auth/start?nonce="
    std::function<AuthToken()> randomToken;   // cryptographically random in production
    float spawnX = 0, spawnY = 0, spawnZ = 0;  // where new characters appear
    bool allowTestBots = false;  // accept fake ids < kMaxTestBotSteamId without auth (testing only)
};

class SessionManager {
public:
    explicit SessionManager(SessionConfig cfg);

    std::vector<Outgoing> onConnect(ConnId conn, double nowSec = 0.0);
    std::vector<Outgoing> onPacket(ConnId conn, std::span<const uint8_t> bytes, double nowSec);
    std::vector<Outgoing> onDisconnect(ConnId conn);
    std::vector<Outgoing> tick(double nowSec);  // timeouts + periodic position saves
    // Steam OpenID succeeded for the connection waiting on `nonce`; `steamId` is the verified one.
    std::vector<Outgoing> onAuthVerified(const std::string& nonce, uint64_t steamId, double nowSec);

    // Connections the transport must close; cleared by each call.
    std::vector<ConnId> connectionsToClose();

    size_t playerCount() const;

private:
    struct Conn {
        double lastSeen = 0.0;
        uint32_t badPackets = 0;
        std::optional<PlayerId> player;  // set once accepted
        std::optional<PlayerState> lastState;
        uint64_t steamId = 0;            // verified (or token-checked) identity
        std::string nonce;               // pending Steam OpenID
        bool hasCharacter = false;
        bool positionDirty = false;
        double lastSaved = 0.0;
    };

    std::vector<Outgoing> handleHello(ConnId conn, Conn& c, const Hello& h, double nowSec);
    std::vector<Outgoing> handleState(ConnId conn, Conn& c, PlayerState s);
    std::vector<Outgoing> handleCreate(ConnId conn, Conn& c, const CreateCharacter& m);
    std::vector<Outgoing> accept(ConnId conn, Conn& c, const AuthToken& newToken, double nowSec);
    std::vector<Outgoing> reject(ConnId conn, RejectReason reason);
    void savePosition(Conn& c);
    std::vector<Outgoing> dropConn(ConnId conn);  // removes and broadcasts PlayerLeft if needed
    void requestClose(ConnId conn);

    SessionConfig cfg_;
    std::unordered_map<ConnId, Conn> conns_;
    std::unordered_map<std::string, ConnId> pendingAuth_;
    std::vector<ConnId> toClose_;
    PlayerId nextPlayerId_ = 1;
};

}  // namespace wok
