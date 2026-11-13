#include "game_memory.hpp"

#include <windows.h>

#include <cstring>

namespace wok::mem {

uintptr_t moduleBase() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    return base;
}

std::span<const uint8_t> moduleText() {
    auto base = reinterpret_cast<const uint8_t*>(moduleBase());
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (std::memcmp(sec->Name, ".text", 5) == 0)
            return {base + sec->VirtualAddress, sec->Misc.VirtualSize};
    return {};
}

bool readBytes(uintptr_t addr, void* out, size_t n) {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFFFull - n) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool readPtr(uintptr_t addr, uintptr_t& out) {
    if (addr & 7) return false;
    return readBytes(addr, &out, sizeof(out));
}

bool isA(uintptr_t obj, const char* rttiName) {
    // MSVC x64: vtable[-1] -> CompleteObjectLocator { sig, offset, cdOffset, rva TypeDescriptor, ... };
    // TypeDescriptor { pVFTable, spare, char name[] }.
    uintptr_t vt, col;
    if (!readPtr(obj, vt) || !readPtr(vt - 8, col)) return false;
    uint32_t sigAndTd[4];
    if (!readBytes(col, sigAndTd, sizeof(sigAndTd)) || sigAndTd[0] != 1 || sigAndTd[1] != 0) return false;
    char name[64] = {};
    size_t len = std::strlen(rttiName) + 1;
    if (len > sizeof(name) || !readBytes(moduleBase() + sigAndTd[3] + 16, name, len)) return false;
    return std::memcmp(name, rttiName, len) == 0;
}

}  // namespace wok::mem
