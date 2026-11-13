#pragma once
// Crash-safe access to game memory (SEH-guarded reads) and RTTI-based class checks.

#include <cstdint>
#include <span>

namespace wok::mem {

uintptr_t moduleBase();
std::span<const uint8_t> moduleText();  // .text section of kenshi_x64.exe

bool readPtr(uintptr_t addr, uintptr_t& out);
bool readBytes(uintptr_t addr, void* out, size_t n);

// True if `obj` is a polymorphic object whose most-derived RTTI name equals `rttiName`
// (e.g. ".?AVCharacterHuman@@").
bool isA(uintptr_t obj, const char* rttiName);

}  // namespace wok::mem
