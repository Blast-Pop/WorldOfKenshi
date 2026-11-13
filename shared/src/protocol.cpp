#include <wok/bytes.hpp>
#include <wok/protocol.hpp>

#include <cmath>
#include <cstring>

namespace wok {

bool Hello::operator==(const Hello& o) const {
    return protocolVersion == o.protocolVersion && steamId == o.steamId &&
           std::memcmp(name, o.name, sizeof(name)) == 0 && token == o.token;
}

namespace {

void writeBytes(ByteWriter& w, const void* data, size_t n) {
    w.u16(static_cast<uint16_t>(n));
    w.raw(data, n);
}
void writeString(ByteWriter& w, const std::string& s) { writeBytes(w, s.data(), s.size()); }

// Reads a u16-prefixed field; fails the reader-independent check when longer than `limit`.
bool readBytes(ByteReader& r, size_t limit, std::string& out) {
    uint16_t n = r.u16();
    if (!r.ok() || n > limit) return false;
    out.assign(n, '\0');
    r.raw(out.data(), n);
    return r.ok();
}
bool readBytes(ByteReader& r, size_t limit, std::vector<uint8_t>& out) {
    uint16_t n = r.u16();
    if (!r.ok() || n > limit) return false;
    out.assign(n, 0);
    r.raw(out.data(), n);
    return r.ok();
}

void write(ByteWriter& w, const Hello& m) {
    w.u16(m.protocolVersion);
    w.u64(m.steamId);
    w.raw(m.name, sizeof(m.name));
    w.raw(m.token.data(), m.token.size());
}
void write(ByteWriter& w, const HelloAck& m) {
    w.u8(static_cast<uint8_t>(m.reason));
    w.u32(m.playerId);
    w.u8(m.hasCharacter ? 1 : 0);
    w.raw(m.token.data(), m.token.size());
}
void write(ByteWriter& w, const PlayerState& m) {
    w.u32(m.playerId);
    w.u32(m.tick);
    w.f32(m.x);
    w.f32(m.y);
    w.f32(m.z);
    w.f32(m.yaw);
    w.u8(static_cast<uint8_t>(m.anim));
}
void write(ByteWriter& w, const PlayerLeft& m) { w.u32(m.playerId); }
void write(ByteWriter&, const Ping&) {}
void write(ByteWriter& w, const AuthChallenge& m) { writeString(w, m.url); }
void write(ByteWriter& w, const CharacterData& m) {
    writeString(w, m.name);
    writeString(w, m.race);
    writeBytes(w, m.appearance.data(), m.appearance.size());
    w.f32(m.x);
    w.f32(m.y);
    w.f32(m.z);
    w.f32(m.yaw);
}
void write(ByteWriter& w, const CreateCharacter& m) {
    writeString(w, m.name);
    writeString(w, m.race);
    writeBytes(w, m.appearance.data(), m.appearance.size());
}

constexpr MsgType typeOf(const Hello&) { return MsgType::Hello; }
constexpr MsgType typeOf(const HelloAck&) { return MsgType::HelloAck; }
constexpr MsgType typeOf(const PlayerState&) { return MsgType::PlayerState; }
constexpr MsgType typeOf(const PlayerLeft&) { return MsgType::PlayerLeft; }
constexpr MsgType typeOf(const Ping&) { return MsgType::Ping; }
constexpr MsgType typeOf(const AuthChallenge&) { return MsgType::AuthChallenge; }
constexpr MsgType typeOf(const CharacterData&) { return MsgType::CharacterData; }
constexpr MsgType typeOf(const CreateCharacter&) { return MsgType::CreateCharacter; }

bool finite(float f) { return std::isfinite(f); }

std::optional<Message> readBody(MsgType type, ByteReader& r) {
    switch (type) {
        case MsgType::Hello: {
            Hello m;
            m.protocolVersion = r.u16();
            m.steamId = r.u64();
            r.raw(m.name, sizeof(m.name));
            r.raw(m.token.data(), m.token.size());
            if (std::memchr(m.name, 0, sizeof(m.name)) == nullptr) return std::nullopt;
            return m;
        }
        case MsgType::HelloAck: {
            HelloAck m;
            uint8_t reason = r.u8();
            if (reason > static_cast<uint8_t>(RejectReason::ModMismatch)) return std::nullopt;
            m.reason = static_cast<RejectReason>(reason);
            m.playerId = r.u32();
            uint8_t has = r.u8();
            if (has > 1) return std::nullopt;
            m.hasCharacter = has == 1;
            r.raw(m.token.data(), m.token.size());
            return m;
        }
        case MsgType::PlayerState: {
            PlayerState m;
            m.playerId = r.u32();
            m.tick = r.u32();
            m.x = r.f32();
            m.y = r.f32();
            m.z = r.f32();
            m.yaw = r.f32();
            uint8_t anim = r.u8();
            if (!finite(m.x) || !finite(m.y) || !finite(m.z) || !finite(m.yaw)) return std::nullopt;
            if (anim > static_cast<uint8_t>(AnimState::Run)) return std::nullopt;
            m.anim = static_cast<AnimState>(anim);
            return m;
        }
        case MsgType::PlayerLeft: {
            PlayerLeft m;
            m.playerId = r.u32();
            return m;
        }
        case MsgType::Ping:
            return Ping{};
        case MsgType::AuthChallenge: {
            AuthChallenge m;
            if (!readBytes(r, kMaxUrlBytes, m.url)) return std::nullopt;
            return m;
        }
        case MsgType::CharacterData: {
            CharacterData m;
            if (!readBytes(r, kMaxNameBytes, m.name) || !readBytes(r, kMaxStringIdBytes, m.race) ||
                !readBytes(r, kMaxAppearanceBytes, m.appearance))
                return std::nullopt;
            m.x = r.f32();
            m.y = r.f32();
            m.z = r.f32();
            m.yaw = r.f32();
            if (!finite(m.x) || !finite(m.y) || !finite(m.z) || !finite(m.yaw)) return std::nullopt;
            return m;
        }
        case MsgType::CreateCharacter: {
            CreateCharacter m;
            if (!readBytes(r, kMaxNameBytes, m.name) || !readBytes(r, kMaxStringIdBytes, m.race) ||
                !readBytes(r, kMaxAppearanceBytes, m.appearance))
                return std::nullopt;
            return m;
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<uint8_t> encode(const Message& msg) {
    ByteWriter w;
    std::visit(
        [&](const auto& m) {
            w.u8(static_cast<uint8_t>(typeOf(m)));
            write(w, m);
        },
        msg);
    return w.take();
}

std::optional<Message> decode(std::span<const uint8_t> bytes) {
    ByteReader r(bytes);
    uint8_t type = r.u8();
    if (!r.ok() || type < static_cast<uint8_t>(MsgType::Hello) ||
        type > static_cast<uint8_t>(MsgType::CreateCharacter))
        return std::nullopt;
    auto msg = readBody(static_cast<MsgType>(type), r);
    if (!msg || !r.ok() || !r.atEnd()) return std::nullopt;
    return msg;
}

}  // namespace wok
