#pragma once
// Crash-safe access to game memory (SEH-guarded reads) and RTTI-based class checks.

#include <cstdint>
#include <span>
#include <string>

namespace wok::mem {

uintptr_t moduleBase();
std::span<const uint8_t> moduleText();  // .text section of kenshi_x64.exe

bool readPtr(uintptr_t addr, uintptr_t& out);
bool readBytes(uintptr_t addr, void* out, size_t n);

// True if `obj` is a polymorphic object whose most-derived RTTI name equals `rttiName`
// (e.g. ".?AVCharacterHuman@@").
bool isA(uintptr_t obj, const char* rttiName);

// Reads an MSVC 2010 std::string stored at `addr` (16-byte SSO buffer, size +0x10, capacity +0x18).
// Empty if unreadable or implausible (longer than `maxLen`).
std::string readString(uintptr_t addr, size_t maxLen = 256);

}  // namespace wok::mem
