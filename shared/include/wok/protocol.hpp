#pragma once
// World of Kenshi wire protocol, version 9.
// Every message is: 1 byte MsgType, then the fields in declaration order, little-endian.
// Variable-length fields are a u16 byte count followed by the bytes, with hard upper limits.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wok {

constexpr uint16_t WOK_PROTOCOL_VERSION = 9;
constexpr uint16_t WOK_DEFAULT_PORT = 27800;

constexpr size_t kMaxNameBytes = 31;
constexpr size_t kMaxStringIdBytes = 64;   // FCS stringIDs ("17-gamedata.quack", "51707-rebirth.mod")
constexpr size_t kMaxAppearanceBytes = 16384;  // includes Kenshi's exported body file (base64)
constexpr size_t kMaxUrlBytes = 512;
constexpr size_t kMaxServerNameBytes = 63;
constexpr size_t kMaxServerDescriptionBytes = 255;
constexpr size_t kMaxStats = 64;
constexpr size_t kMaxTicketBytes = 512;  // identity ticket from the master server
constexpr size_t kMaxInventoryBytes = 32768;  // the client's inventory text (opaque to the server)  // skill ids are 1..kMaxStats (docs/re/stats.md)

using PlayerId = uint32_t;
using ConnId = uint32_t;  // connection handle assigned by the transport
using AuthToken = std::array<uint8_t, 32>;  // per-server secret; all zero = none

enum class MsgType : uint8_t {
    Hello = 1,
    HelloAck = 2,
    PlayerState = 3,
    PlayerLeft = 4,
    Ping = 5,
    AuthChallenge = 6,
    CharacterData = 7,
    CreateCharacter = 8,
    ServerInfoRequest = 9,
    ServerInfo = 10,
    StatsUpdate = 11,
    InventoryUpdate = 12,
    WorldTime = 13,
    PlayerInfo = 14,
    CharacterDied = 15,
};
enum class RejectReason : uint8_t {
    None = 0,
    ServerFull = 1,
    VersionMismatch = 2,
    Banned = 3,
    BadAuth = 4,
    ModMismatch = 5,  // reserved for the server-imposed mod list (spec 7.6)
};
enum class AnimState : uint8_t { Idle = 0, Walk = 1, Run = 2 };

struct Hello {
    uint16_t protocolVersion = WOK_PROTOCOL_VERSION;
    uint64_t steamId = 0;
    char name[32] = {};  // must contain a NUL terminator
    AuthToken token{};   // token issued by this server earlier, zero on a first visit
    std::string ticket;  // identity ticket signed by the master server for this server, may be empty
    bool operator==(const Hello&) const;
};

struct HelloAck {
    RejectReason reason = RejectReason::None;  // None => accepted
    PlayerId playerId = 0;
    bool hasCharacter = false;  // true => a CharacterData follows
    AuthToken token{};          // non-zero only when the server issues a new token
    bool operator==(const HelloAck&) const = default;
};

struct PlayerState {
    PlayerId playerId = 0;
    uint32_t tick = 0;
    float x = 0, y = 0, z = 0;
    float yaw = 0;  // degrees
    AnimState anim = AnimState::Idle;
    bool operator==(const PlayerState&) const = default;
};

struct PlayerLeft {
    PlayerId playerId = 0;
    bool operator==(const PlayerLeft&) const = default;
};

// Keepalive sent by the client about once per second, so a player who is in a menu or a loading
// screen (and therefore has no PlayerState to send) is not timed out.
struct Ping {
    bool operator==(const Ping&) const = default;
};

// First visit: the player must prove their SteamID through Steam OpenID at this URL (docs/m2-auth.md).
struct AuthChallenge {
    std::string url;
    bool operator==(const AuthChallenge&) const = default;
};

// One skill of a character: Kenshi's stat id and its value (level + progress, e.g. 12.37).
struct StatValue {
    uint8_t id = 0;
    float value = 0;
    bool operator==(const StatValue&) const = default;
};

// The player's server-side character (server -> client).
struct CharacterData {
    std::string name;
    std::string race;                  // FCS stringID
    std::vector<uint8_t> appearance;   // opaque, produced by the client from Kenshi's character editor
    std::string templateId;            // FCS stringID of the character template (e.g. a start's character)
    float x = 0, y = 0, z = 0, yaw = 0;
    std::vector<StatValue> stats;      // saved skills (empty for a new character)
    std::vector<uint8_t> inventory;    // saved inventory, opaque (empty: keep what the game gives)
    bool operator==(const CharacterData&) const = default;
};

// A new character made in Kenshi's character editor (client -> server). The server picks the spawn point.
struct CreateCharacter {
    std::string name;
    std::string race;
    std::vector<uint8_t> appearance;
    std::string templateId;
    // Where Kenshi's chosen start put the character (used unless the server forces a spawn point).
    bool hasStart = false;
    float startX = 0, startY = 0, startZ = 0;
    bool operator==(const CreateCharacter&) const = default;
};

// Server browser: allowed before Hello, answered with ServerInfo (the client measures the ping).
struct ServerInfoRequest {
    bool operator==(const ServerInfoRequest&) const = default;
};

struct ServerInfo {
    uint16_t protocolVersion = WOK_PROTOCOL_VERSION;
    std::string name;
    std::string description;
    uint16_t players = 0;
    uint16_t maxPlayers = 0;
    bool operator==(const ServerInfo&) const = default;
};

// Skills that changed since the last update (client -> server); the server keeps the latest value.
struct StatsUpdate {
    std::vector<StatValue> values;
    bool operator==(const StatsUpdate&) const = default;
};

// The character's whole inventory, sent when it changes (client -> server).
struct InventoryUpdate {
    std::vector<uint8_t> items;
    bool operator==(const InventoryUpdate&) const = default;
};

// The server's world clock (server -> client): Kenshi day (from 1) and hour of the day (0-24).
struct WorldTime {
    uint32_t day = 1;
    float hour = 0;
    bool operator==(const WorldTime&) const = default;
};

// Permanent death. Client -> server: my character died. Server -> client: it is gone, make a new one.
struct CharacterDied {
    bool operator==(const CharacterDied&) const = default;
};

// Who another player is (server -> client): shown above their avatar.
struct PlayerInfo {
    PlayerId playerId = 0;
    std::string name;  // their character's name
    bool operator==(const PlayerInfo&) const = default;
};

using Message = std::variant<Hello, HelloAck, PlayerState, PlayerLeft, Ping, AuthChallenge, CharacterData,
                             CreateCharacter, ServerInfoRequest, ServerInfo, StatsUpdate, InventoryUpdate, WorldTime,
                             PlayerInfo, CharacterDied>;

std::vector<uint8_t> encode(const Message& msg);
// Returns nullopt for any malformed input: unknown type, truncated, trailing bytes, out-of-range
// enum, non-finite float, name without NUL terminator, or a variable field over its limit.
std::optional<Message> decode(std::span<const uint8_t> bytes);

}  // namespace wok
