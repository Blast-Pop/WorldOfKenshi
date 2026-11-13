#pragma once
// World of Kenshi wire protocol, version 1.
// Every message is: 1 byte MsgType, then the fields in declaration order, little-endian.

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace wok {

constexpr uint16_t WOK_PROTOCOL_VERSION = 1;
constexpr uint16_t WOK_DEFAULT_PORT = 27800;

using PlayerId = uint32_t;
using ConnId = uint32_t;  // connection handle assigned by the transport

enum class MsgType : uint8_t { Hello = 1, HelloAck = 2, PlayerState = 3, PlayerLeft = 4 };
enum class RejectReason : uint8_t { None = 0, ServerFull = 1, VersionMismatch = 2, Banned = 3, BadAuth = 4 };
enum class AnimState : uint8_t { Idle = 0, Walk = 1, Run = 2 };

struct Hello {
    uint16_t protocolVersion = WOK_PROTOCOL_VERSION;
    uint64_t steamId = 0;
    char name[32] = {};  // must contain a NUL terminator
    bool operator==(const Hello&) const;
};

struct HelloAck {
    RejectReason reason = RejectReason::None;  // None => accepted
    PlayerId playerId = 0;
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

using Message = std::variant<Hello, HelloAck, PlayerState, PlayerLeft>;

std::vector<uint8_t> encode(const Message& msg);
// Returns nullopt for any malformed input: unknown type, truncated, trailing bytes,
// out-of-range enum, non-finite float, or a name without NUL terminator.
std::optional<Message> decode(std::span<const uint8_t> bytes);

}  // namespace wok
