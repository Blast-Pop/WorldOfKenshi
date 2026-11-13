#pragma once
// Byte-signature scanning. Pure functions over memory spans, so they are unit-tested
// outside the game. Patterns look like "48 8B 05 ?? ?? ?? ?? 48 85 C0" ("?" also accepted).

#include <cstdint>
#include <span>

namespace wok {

struct Signature {
    const char* name;
    const char* pattern;
    int offset = 0;
    enum Kind { Direct, RipRel32 } kind = Direct;
};

// First match of `pattern` in `region`, or nullptr if absent or if the pattern is malformed.
const uint8_t* scan(std::span<const uint8_t> region, const char* pattern);

// Address (in the region's runtime address space, starting at regionBase) designated by `sig`:
//   Direct   -> regionBase + matchIndex + offset
//   RipRel32 -> (regionBase + matchIndex + offset + 4) + int32 stored at (match + offset)
// Returns 0 if not found (and logs "signature not found: <name>" when a logger is installed).
uintptr_t resolve(std::span<const uint8_t> region, uintptr_t regionBase, const Signature& sig);

using SigLogFn = void (*)(const char* message);
void setSigLogger(SigLogFn fn);

}  // namespace wok
