#pragma once
// Pure session logic of the dedicated server: no sockets, fully unit-testable.
// The transport feeds events in; the manager returns the messages to send.
// With a CharacterStore, players authenticate (token, or Steam OpenID on a first visit - see
// docs/m2-auth.md) and their characters live on the server.

#include <wok/protocol.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wok {

class CharacterStore;

// Progress (position, skills...) is kept in memory and written in small batches (spec 7.7).
inline constexpr double kProgressSaveIntervalSec = 2.0;
// Clients realign their game clock to the server's this often.
inline constexpr double kWorldTimeIntervalSec = 5.0;
// Steam answers a ticket check within a second or two; past this the browser login is offered.
inline constexpr double kSteamAuthTimeoutSec = 10.0;
// Test bots (wok_testclient) use fake ids below this value; real SteamID64s start at 76561197960265728.
inline constexpr uint64_t kMaxTestBotSteamId = 1000000;
// A Steam login in progress survives reconnects of the same claimed SteamID for this long.
inline constexpr double kPendingAuthTtlSec = 300.0;
// The world's NPC records: saved this often, and no more than this many.
inline constexpr double kNpcSaveIntervalSec = 5.0;
inline constexpr size_t kMaxNpcRecords = 50000;

struct Outgoing {
    ConnId to;
    Message msg;
};

struct SpawnPoint {
    std::string name;  // e.g. a popular town
    float x = 0, y = 0, z = 0;
};

struct SessionConfig {
    uint32_t maxPlayers = 250;
    double timeoutSec = 10.0;
    uint32_t maxBadPackets = 3;
    std::unordered_set<uint64_t> bannedSteamIds;
    std::string serverName = "World of Kenshi server";  // shown in the server browser
    std::string serverDescription;

    // Server-side characters. Without a store, any Hello is accepted (no authentication).
    CharacterStore* store = nullptr;
    std::string authUrlBase;                  // e.g. "http://host:27801/auth/start?nonce="
    std::function<AuthToken()> randomToken;   // cryptographically random in production
    float spawnX = 0, spawnY = 0, spawnZ = 0;  // fallback spawn when no spawn points are configured
    std::vector<SpawnPoint> spawnPoints;          // new characters appear at one of these
    std::function<size_t(size_t count)> pickSpawn;  // index in [0, count); random when empty
    bool forceSpawn = false;
    // Mods every player must run, in load order (spec 7.6). Empty optional: any mods are accepted.
    std::optional<std::vector<std::string>> requiredMods;
    float chatLocalRadius = 100.0f;  // local chat reaches players this close to the speaker (game units)  // ignore the start position chosen in Kenshi (always use the spawn rule)
    std::function<WorldTime(double nowSec)> worldTime;  // the server's clock; none: no time sync
    // Steam session tickets (docs/m2-auth.md): when set, a Hello carrying a ticket is checked with
    // Steam instead of the browser login. beginSteamAuth returns true if Steam accepted the ticket
    // for checking; steamLicense then answers 0 (owns Kenshi), 1 (does not) or 2 (still checking).
    std::function<bool(uint64_t steamId, const std::string& ticket)> beginSteamAuth;
    std::function<int(uint64_t steamId)> steamLicense;
    std::function<void(uint64_t steamId)> endSteamAuth;
    bool allowTestBots = false;  // accept fake ids < kMaxTestBotSteamId without auth (testing only)
};

class SessionManager {
public:
    explicit SessionManager(SessionConfig cfg);

    std::vector<Outgoing> onConnect(ConnId conn, double nowSec = 0.0);
    std::vector<Outgoing> onPacket(ConnId conn, std::span<const uint8_t> bytes, double nowSec);
    std::vector<Outgoing> onDisconnect(ConnId conn);
    std::vector<Outgoing> tick(double nowSec);  // timeouts, periodic saves, world time broadcast
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
        uint64_t steamPending = 0;       // SteamID whose session ticket Steam is checking
        double steamPendingSince = 0.0;
        bool hasCharacter = false;
        std::string characterName;       // shown to the other players
        std::string characterRace, characterTemplate;
        std::vector<uint8_t> characterLook;  // what the other players' avatars wear
        double nextChat = 0;             // anti-spam: earliest time for the next message
        std::string helloName;           // name sent in Hello (test bots have no character)
        bool positionDirty = false;
        std::map<uint8_t, float> stats;  // latest known skills of the character
        bool statsDirty = false;
        std::vector<uint8_t> inventory;  // latest known inventory (opaque)
        bool inventoryDirty = false;
        double lastSaved = 0.0;
        std::unordered_map<uint32_t, uint32_t> npcKeys;  // this owner's NPC key -> world record
    };

    std::vector<Outgoing> handleHello(ConnId conn, Conn& c, const Hello& h, double nowSec);
    std::vector<Outgoing> handleState(ConnId conn, Conn& c, PlayerState s);
    std::vector<Outgoing> handleCreate(ConnId conn, Conn& c, const CreateCharacter& m);
    void handleStats(Conn& c, const StatsUpdate& m);
    std::vector<Outgoing> accept(ConnId conn, Conn& c, const AuthToken& newToken, double nowSec);
    std::vector<Outgoing> reject(ConnId conn, RejectReason reason);
    std::vector<Outgoing> handleChat(ConnId conn, Conn& c, const ChatSend& m, double nowSec);
    // Shared world (spec 7.3): recompute who simulates each occupied cell; the new list if it changed.
    std::vector<Outgoing> updateCellOwners();
    // An owner's NPC message, relayed to the players near it.
    std::vector<Outgoing> relayNpc(ConnId conn, Conn& c, Message m);
    // The world's memory: every NPC an owner reported, kept (and saved) for the next owner.
    struct NpcMemory {
        NpcRecord rec;
        bool placed = false;  // a position was reported
        bool dirty = false;   // changed since the last save
    };
    void rememberNpc(Conn& c, const Message& m);
    void handleNpcBind(ConnId conn, Conn& c, const NpcBind& m);
    // The unclaimed records of the cells `player` just got, for it to match its own NPCs against.
    std::vector<Outgoing> recordsFor(ConnId conn, const std::vector<std::pair<int32_t, int32_t>>& gained);
    std::map<uint32_t, NpcMemory> npcRecords_;
    uint32_t nextNpcRecord_ = 1;
    double nextNpcSave_ = 0;
    std::map<std::pair<int32_t, int32_t>, PlayerId> cellOwners_;
    double nextCellUpdate_ = 0;
    void savePosition(Conn& c);
    std::vector<Outgoing> dropConn(ConnId conn);  // removes and broadcasts PlayerLeft if needed
    void requestClose(ConnId conn);

    SessionConfig cfg_;
    std::unordered_map<ConnId, Conn> conns_;
    struct PendingAuth {
        ConnId conn = 0;                  // 0 while the player is not connected
        uint64_t claimedSteamId = 0;
        double created = 0.0;
        uint64_t verifiedSteamId = 0;     // set if Steam answered while the player was away
    };
    std::unordered_map<std::string, PendingAuth> pendingAuth_;     // by nonce
    std::unordered_map<uint64_t, std::string> pendingByClaimed_;   // claimed SteamID -> nonce
    std::vector<Outgoing> completeAuth(ConnId conn, Conn& c, uint64_t steamId, double nowSec);
    std::vector<ConnId> toClose_;
    PlayerId nextPlayerId_ = 1;
    double lastWorldTimeSent_ = 0.0;
};

}  // namespace wok
