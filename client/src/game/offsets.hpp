#pragma once
// Game structure knowledge for Kenshi 1.0.68. See docs/re/game-world.md and docs/re/character.md.
// Code addresses are found by signature at runtime; class identity uses the game's own RTTI names.

#include <core/sigscan.hpp>

#include <cstdint>

namespace wok::offsets {

// `mov rax, [rip+X]; mov rdi, [rax+0x2a0]; lea ...` - X is the PlayerInterface* slot (GameWorld+0x580).
inline constexpr Signature kPlayerInterfaceSlot{"PlayerInterfaceSlot",
                                                "48 8B 05 ?? ?? ?? ?? 48 8B B8 A0 02 00 00 48 8D", 3,
                                                Signature::RipRel32};

// PlayerInterface fields
inline constexpr uintptr_t kPiSelectedChars = 0x2c0;  // -> Character*[] (selection), [0] = selected

// Character fields
inline constexpr uintptr_t kCharPosition = 0x48;     // float x, y(height), z
inline constexpr uintptr_t kCharOrientation = 0xb0;  // quaternion w, x, y, z

// RTTI class names (as stored in the game's type descriptors)
inline constexpr const char* kRttiCharacterHuman = ".?AVCharacterHuman@@";

}  // namespace wok::offsets
