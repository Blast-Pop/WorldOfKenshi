#pragma once
// World of Kenshi wire protocol, version 2.
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

constexpr uint16_t WOK_PROTOCOL_VERSION = 2;
constexpr uint16_t WOK_DEFAULT_PORT = 27800;

constexpr size_t kMaxNameBytes = 31;
constexpr size_t kMaxStringIdBytes = 64;   // FCS stringIDs ("17-gamedata.quack", "51707-rebirth.mod")
constexpr size_t kMaxAppearanceBytes = 4096;
constexpr size_t kMaxUrlBytes = 512;

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

// The player's server-side character (server -> client).
struct CharacterData {
    std::string name;
    std::string race;                  // FCS stringID
    std::vector<uint8_t> appearance;   // opaque, produced by the client from Kenshi's character editor
    float x = 0, y = 0, z = 0, yaw = 0;
    bool operator==(const CharacterData&) const = default;
};

// A new character made in Kenshi's character editor (client -> server). The server picks the spawn point.
struct CreateCharacter {
    std::string name;
    std::string race;
    std::vector<uint8_t> appearance;
    bool operator==(const CreateCharacter&) const = default;
};

using Message =
    std::variant<Hello, HelloAck, PlayerState, PlayerLeft, Ping, AuthChallenge, CharacterData, CreateCharacter>;

std::vector<uint8_t> encode(const Message& msg);
// Returns nullopt for any malformed input: unknown type, truncated, trailing bytes, out-of-range
// enum, non-finite float, name without NUL terminator, or a variable field over its limit.
std::optional<Message> decode(std::span<const uint8_t> bytes);

}  // namespace wok
