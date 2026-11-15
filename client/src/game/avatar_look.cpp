#include "avatar_look.hpp"

#include "../core/paths.hpp"
#include "../log.hpp"
#include "../ui/mygui_abi.hpp"
#include "game_memory.hpp"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

namespace wok {

namespace {

// Kenshi 1.0.68: the editor's import (callback RVA 0x5f4280) and its apply to the character.
constexpr uintptr_t kGameDataCtor = 0x66b590;          // GameData::GameData(this)
constexpr uintptr_t kCopyStandaloneVtable = 0x1683098;  // GameDataCopyStandalone vftable
constexpr uintptr_t kLoadBodyFile = 0x6c3a70;           // (GameData*, const std::string& path, int 0x42) -> bool
constexpr uintptr_t kSetAppearanceData = 0x5b9e90;      // Character::setAppearanceData(Character*, GameDataCopyStandalone*)
constexpr size_t kGameDataSize = 0x300;                 // what Kenshi allocates for one GameData
constexpr int kAppearanceType = 0x42;

constexpr uint8_t kCtorPrologue[] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x57};
constexpr uint8_t kLoadPrologue[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr uint8_t kApplyPrologue[] = {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x54};

template <size_t N>
bool codeIs(uintptr_t rva, const uint8_t (&bytes)[N]) {
    uint8_t got[N];
    return mem::readBytes(mem::moduleBase() + rva, got, N) && std::memcmp(got, bytes, N) == 0;
}

bool guardedApply(uintptr_t character, uint8_t* gd, const mygui::Vc10String* path) {
    const uintptr_t base = mem::moduleBase();
    __try {
        reinterpret_cast<void* (*)(void*)>(base + kGameDataCtor)(gd);
        *reinterpret_cast<uintptr_t*>(gd) = base + kCopyStandaloneVtable;  // as the editor does
        gd[0x18] = 1;
        const bool loaded =
            reinterpret_cast<bool (*)(void*, const void*, int)>(base + kLoadBodyFile)(gd, path, kAppearanceType);
        if (!loaded) return false;
        reinterpret_cast<void (*)(uintptr_t, void*)>(base + kSetAppearanceData)(character, gd);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool applyBodyFile(uintptr_t character, const std::vector<uint8_t>& bod2, const std::string& tag) {
    static const bool usable = codeIs(kGameDataCtor, kCtorPrologue) && codeIs(kLoadBodyFile, kLoadPrologue) &&
                               codeIs(kSetAppearanceData, kApplyPrologue);
    if (!usable || !character || bod2.empty() || !mem::isA(character, ".?AVCharacterHuman@@")) return false;
    const std::string dir = modFile(modDir(), "avatars");
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string file = dir + "\\" + tag + ".bod2";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bod2.data()), static_cast<std::streamsize>(bod2.size()));
        if (!out) return false;
    }
    // The appearance record stays alive: the character may keep pointing at it (a few hundred bytes
    // per avatar spawn).
    // Allocated like Kenshi allocates its GameData (Ogre's allocator), in case the game frees it.
    using OgreNewFn = void* (*)(size_t);
    static OgreNewFn ogreNew = [] {
        HMODULE m = GetModuleHandleA("OgreMain_x64.dll");
        return m ? reinterpret_cast<OgreNewFn>(GetProcAddress(
                       m, "??2?$AllocatedObject@V?$CategorisedAllocPolicy@$0A@@Ogre@@@Ogre@@SAPEAX_K@Z"))
                 : nullptr;
    }();
    auto* gd = static_cast<uint8_t*>(ogreNew ? ogreNew(kGameDataSize) : std::calloc(1, kGameDataSize));
    if (!gd) return false;
    std::memset(gd, 0, kGameDataSize);
    mygui::Vc10String path(file);
    const bool ok = guardedApply(character, gd, &path);
    log("avatar: exact look %s from %s", ok ? "applied" : "NOT applied", file.c_str());
    return ok;
}

}  // namespace wok
