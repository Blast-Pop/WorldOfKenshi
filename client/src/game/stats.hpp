#pragma once
// Reads and writes a character's skills (Kenshi's CharStats). Offsets: docs/re/stats.md.

#include <wok/protocol.hpp>

#include <cstdint>
#include <vector>

namespace wok {

// Every per-character skill, by Kenshi's stat id; empty if the character is not readable.
std::vector<StatValue> readStats(uintptr_t character);
// Applies saved skills to the character; unknown ids are ignored. Returns how many were written.
int writeStats(uintptr_t character, const std::vector<StatValue>& stats);

// Dead: Kenshi's death routine (RVA 0x7a6200) sets character + 0x5bc; also the head, chest or
// stomach at -100 or below (caught before that routine runs).
bool isDead(uintptr_t character);

// Body health for the shared world: blood and the body parts (no hunger), ids as in StatsUpdate.
std::vector<StatValue> readBodyHealth(uintptr_t character);
void writeBodyHealth(uintptr_t character, const std::vector<StatValue>& values);
// Takes `damage` (health lost, per id) off the character's body.
void applyBodyDamage(uintptr_t character, const std::vector<StatValue>& damage);

// Hunger (character + 0x4b8, float, 300 = full): read and write.
bool readHunger(uintptr_t character, float& out);
void writeHunger(uintptr_t character, float value);

}  // namespace wok
