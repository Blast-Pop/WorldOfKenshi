#include "stats.hpp"

#include "game_memory.hpp"
#include "../log.hpp"

#include <windows.h>

#include <cstring>

namespace wok {

namespace {

constexpr uintptr_t kCharStats = 0x450;  // Character -> CharStats

struct StatSlot {
    uint8_t id;
    uint16_t offset;  // float inside CharStats
};
// Kenshi 1.0.68 stat ids and their storage (ids 14/15 alias First Aid, 20/33 have no storage).
constexpr StatSlot kSlots[] = {
    {1, 0x80},   {2, 0x120},  {3, 0xe4},   {4, 0xe0},   {5, 0xcc},   {6, 0xdc},   {7, 0xd0},
    {8, 0xd4},   {9, 0x98},   {10, 0xac},  {11, 0x114}, {12, 0xe8},  {13, 0xec},  {16, 0xa4},
    {17, 0x94},  {18, 0x88},  {19, 0x124}, {21, 0x90},  {22, 0xb8},  {23, 0xa8},  {24, 0x8c},
    {25, 0xf8},  {26, 0xfc},  {27, 0x100}, {28, 0x108}, {29, 0x104}, {30, 0x10c}, {31, 0x9c},
    {32, 0xf0},  {34, 0x118}, {35, 0x110}, {36, 0xf4},  {37, 0xb0},  {38, 0xd8},
};

uintptr_t statsOf(uintptr_t character) {
    uintptr_t stats = 0;
    if (!character || !mem::isA(character, ".?AVCharacterHuman@@") || !mem::readPtr(character + kCharStats, stats) ||
        !stats || !mem::isA(stats, ".?AVCharStats@@"))
        return 0;
    return stats;
}

bool writeFloat(uintptr_t addr, float v) {
    __try {
        *reinterpret_cast<float*>(addr) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// Health travels with the skills under ids Kenshi never uses (skills are 1..38):
// 48 = blood (MedicalSystem, character + 0x4c8); 49..55 = the 7 body parts' field +0x40 and
// 56..62 = their field +0x54 (HealthPartStatus, Kenshi array at character + 0x5e8). docs/re/health.md.
constexpr uint8_t kBloodId = 48;
constexpr uint8_t kPartFirstId = 49;
constexpr uint8_t kPartSecondId = 56;
constexpr uintptr_t kBlood = 0x4c8;
constexpr uint8_t kHungerId = 64;  // the last id the protocol allows (kMaxStats)
constexpr uintptr_t kHunger = 0x4b8;
constexpr uintptr_t kPartCount = 0x5f0;  // u32
constexpr uintptr_t kParts = 0x5f8;      // HealthPartStatus**
constexpr uintptr_t kPartFieldA = 0x40;
constexpr uintptr_t kPartFieldB = 0x54;
constexpr uint32_t kMaxParts = 7;

std::vector<std::pair<uint8_t, uintptr_t>> healthSlots(uintptr_t character) {
    // Blood (48) and hunger (64, character + 0x4b8, 300 = full: Kenshi's death routine reads it there).
    std::vector<std::pair<uint8_t, uintptr_t>> out{{kBloodId, character + kBlood}, {kHungerId, character + kHunger}};
    uint32_t n = 0;
    uintptr_t parts = 0;
    if (!mem::readBytes(character + kPartCount, &n, 4) || !mem::readPtr(character + kParts, parts) || !parts) return out;
    for (uint32_t i = 0; i < n && i < kMaxParts; ++i) {
        uintptr_t part = 0;
        if (!mem::readPtr(parts + i * 8, part) || !part) continue;
        out.push_back({static_cast<uint8_t>(kPartFirstId + i), part + kPartFieldA});
        out.push_back({static_cast<uint8_t>(kPartSecondId + i), part + kPartFieldB});
    }
    return out;
}

// Money (the player's, shown as "c.1,000"): int at *(*(PlayerInterface + 0x2a0) + 0x80) + 0x88,
// PlayerInterface being at GameWorld + 0x580 (the game's reward code adds to it). Saved as id 63.
constexpr uint8_t kMoneyId = 63;
uintptr_t moneyAddress() {
    uintptr_t pi = 0, owner = 0, holder = 0;
    if (!mem::readPtr(mem::moduleBase() + 0x2134110 + 0x580, pi) || !pi || !mem::readPtr(pi + 0x2a0, owner) || !owner ||
        !mem::readPtr(owner + 0x80, holder) || !holder)
        return 0;
    int v = -1;
    return mem::readBytes(holder + 0x88, &v, sizeof(v)) && v >= 0 && v < 1000000000 ? holder + 0x88 : 0;
}

std::vector<StatValue> readStats(uintptr_t character) {
    std::vector<StatValue> out;
    uintptr_t stats = statsOf(character);
    if (!stats) return out;
    for (const auto& slot : kSlots) {
        float v = 0;
        if (mem::readBytes(stats + slot.offset, &v, sizeof(v)) && v == v && v > -1e6f && v < 1e6f)
            out.push_back({slot.id, v});
    }
    if (uintptr_t money = statsOf(character) ? moneyAddress() : 0) {
        int v = 0;
        if (mem::readBytes(money, &v, sizeof(v))) out.push_back({kMoneyId, static_cast<float>(v)});
    }
    if (statsOf(character))
        for (auto& [id, addr] : healthSlots(character)) {
            float v = 0;
            if (mem::readBytes(addr, &v, sizeof(v)) && v == v && v > -1e4f && v < 1e4f) out.push_back({id, v});
        }
    return out;
}

std::vector<StatValue> readBodyHealth(uintptr_t character) {
    std::vector<StatValue> out;
    for (auto& [id, addr] : healthSlots(character)) {
        if (id == kHungerId) continue;
        float v = 0;
        if (mem::readBytes(addr, &v, sizeof(v)) && v == v && v > -1e4f && v < 1e4f) out.push_back({id, v});
    }
    return out;
}

void writeBodyHealth(uintptr_t character, const std::vector<StatValue>& values) {
    for (auto& [id, addr] : healthSlots(character)) {
        if (id == kHungerId) continue;
        for (const StatValue& v : values)
            if (v.id == id && v.value == v.value && v.value > -1e4f && v.value < 1e4f)
                mem::writeBytes(addr, &v.value, sizeof(v.value));
    }
}

void applyBodyDamage(uintptr_t character, const std::vector<StatValue>& damage) {
    for (auto& [id, addr] : healthSlots(character)) {
        if (id == kHungerId) continue;
        for (const StatValue& d : damage) {
            if (d.id != id || !(d.value > 0 && d.value < 1000.0f)) continue;
            float v = 0;
            if (!mem::readBytes(addr, &v, sizeof(v))) continue;
            v -= d.value;
            mem::writeBytes(addr, &v, sizeof(v));
        }
    }
}

int writeStats(uintptr_t character, const std::vector<StatValue>& values) {
    uintptr_t stats = statsOf(character);
    if (!stats) return 0;
    int written = 0;
    auto health = healthSlots(character);
    for (const auto& v : values) {
        for (const auto& slot : kSlots)
            if (slot.id == v.id && writeFloat(stats + slot.offset, v.value)) ++written;
        for (auto& [id, addr] : health)
            if (id == v.id && writeFloat(addr, v.value)) ++written;
        if (v.id == kMoneyId && v.value >= 0 && v.value < 1e9f)
            if (uintptr_t money = moneyAddress()) {
                int amount = static_cast<int>(v.value + 0.5f);
                if (mem::writeBytes(money, &amount, sizeof(amount))) ++written;
            }
    }
    return written;
}

bool isDead(uintptr_t character) {
    if (!character) return false;
    uint8_t deadFlag = 0;
    if (mem::readBytes(character + 0x5bc, &deadFlag, 1) && deadFlag) return true;
    uint32_t count = 0;
    uintptr_t parts = 0;
    if (!mem::readBytes(character + kPartCount, &count, sizeof(count)) || count < 3 || !mem::readPtr(character + kParts, parts))
        return false;
    for (uint32_t i = 0; i < 3; ++i) {
        uintptr_t part = 0;
        float hp = 0;
        if (mem::readPtr(parts + 8 * i, part) && part && mem::readBytes(part + kPartFieldA, &hp, sizeof(hp)) && hp <= -100.0f)
            return true;
    }
    return false;
}

bool readHunger(uintptr_t character, float& out) {
    return character && mem::readBytes(character + 0x4b8, &out, sizeof(out)) && out >= -1000.0f && out <= 1000.0f;
}

void writeHunger(uintptr_t character, float value) {
    if (character) mem::writeBytes(character + 0x4b8, &value, sizeof(value));
}

}  // namespace wok
