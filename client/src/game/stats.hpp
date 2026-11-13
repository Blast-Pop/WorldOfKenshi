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

}  // namespace wok
