#include "stats.hpp"

#include "game_memory.hpp"

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

std::vector<StatValue> readStats(uintptr_t character) {
    std::vector<StatValue> out;
    uintptr_t stats = statsOf(character);
    if (!stats) return out;
    for (const auto& slot : kSlots) {
        float v = 0;
        if (mem::readBytes(stats + slot.offset, &v, sizeof(v)) && v == v && v > -1e6f && v < 1e6f)
            out.push_back({slot.id, v});
    }
    return out;
}

int writeStats(uintptr_t character, const std::vector<StatValue>& values) {
    uintptr_t stats = statsOf(character);
    if (!stats) return 0;
    int written = 0;
    for (const auto& v : values)
        for (const auto& slot : kSlots)
            if (slot.id == v.id && writeFloat(stats + slot.offset, v.value)) ++written;
    return written;
}

}  // namespace wok
