#include "options_tab.hpp"

#include "../game/game_memory.hpp"
#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace wok {

namespace {

// Kenshi 1.0.68 (docs/re/mygui.md, "Options tabs"). Thunks (jmp) into the real functions.
constexpr uintptr_t kForgottenGui = 0x21337b0;       // the ForgottenGUI object (global, not a pointer)
constexpr uintptr_t kCreateDatapanel = 0x404da;      // (gui, const std::string& name, Widget* parent, bool) -> DatapanelGUI*
constexpr uintptr_t kSetLineSlider = 0x1538e;        // (panel, const std::string& text, int, bool, float min, float max, float* value)
constexpr uintptr_t kMakePanelAny = 0x40476;         // (Any* out, DatapanelGUI** value) -> Any*
constexpr uintptr_t kSetLineCheckbox = 0x158d4;      // (panel, const std::string& text, bool* value, int)
constexpr uintptr_t kSetLine = 0xf6d2;               // (panel, const std::string& name, const std::string& value, int, bool, bool)
constexpr uintptr_t kAddSpace = 0x2d024;             // (panel, int, float)
constexpr const char* kTabName = "Multiplayer";
constexpr int kPanelId = 0x19;        // the id our panel and all its lines share
constexpr float kLineHeight = 28.0f;  // as Kenshi's own option panels

using CreateDatapanelFn = void* (*)(uintptr_t gui, const mygui::Vc10String* name, void* parent, bool flag);
using SetLineSliderFn = void* (*)(void* panel, const mygui::Vc10String* text, int id, bool editable, float min,
                                  float max, float* value);
using MakePanelAnyFn = void* (*)(void* out, void** panel);
using SetLineCheckboxFn = void* (*)(void* panel, const mygui::Vc10String* text, bool* value, int id);
using SetLineFn = void* (*)(void* panel, const mygui::Vc10String* name, const mygui::Vc10String* value, int id,
                           bool a, bool b);
using AddSpaceFn = void (*)(void* panel, int id, float height);
using UStringCtorFn = void* (*)(void* self, const char* text);
using UStringDtorFn = void (*)(void* self);
using AnyEmptyCtorFn = void* (*)(void* self, const void* empty);
using AddItemFn = void* (*)(void* tabControl, const void* name, void* anyByValue);
using FindItemFn = void* (*)(void* tabControl, const void* name);
using SetItemDataFn = void (*)(void* tabControl, void* item, void* anyByValue);

struct MyGuiCalls {
    UStringCtorFn ustring = nullptr;
    UStringDtorFn ustringDtor = nullptr;
    AnyEmptyCtorFn anyEmpty = nullptr;
    const void* anyNull = nullptr;
    AddItemFn addItem = nullptr;
    FindItemFn findItem = nullptr;
    SetItemDataFn setItemData = nullptr;
    bool ok = false;
};

const MyGuiCalls& calls() {
    static MyGuiCalls c = [] {
        MyGuiCalls r;
        HMODULE m = GetModuleHandleA("MyGUIEngine_x64.dll");
        if (!m) return r;
        auto get = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); };
        r.ustring = reinterpret_cast<UStringCtorFn>(get("??0UString@MyGUI@@QEAA@PEBD@Z"));
        r.ustringDtor = reinterpret_cast<UStringDtorFn>(get("??1UString@MyGUI@@QEAA@XZ"));
        r.anyEmpty = reinterpret_cast<AnyEmptyCtorFn>(get("??0Any@MyGUI@@QEAA@AEBUAnyEmpty@01@@Z"));
        r.anyNull = get("?Null@Any@MyGUI@@2UAnyEmpty@12@A");
        r.addItem = reinterpret_cast<AddItemFn>(get("?addItem@TabControl@MyGUI@@QEAAPEAVTabItem@2@AEBVUString@2@VAny@2@@Z"));
        r.findItem = reinterpret_cast<FindItemFn>(get("?findItemWith@TabControl@MyGUI@@QEAAPEAVTabItem@2@AEBVUString@2@@Z"));
        r.setItemData = reinterpret_cast<SetItemDataFn>(get("?setItemData@TabControl@MyGUI@@QEAAXPEAVTabItem@2@VAny@2@@Z"));
        r.ok = r.ustring && r.ustringDtor && r.anyEmpty && r.anyNull && r.addItem && r.findItem && r.setItemData;
        if (!r.ok) log("options: MyGUI exports missing, no Multiplayer tab");
        return r;
    }();
    return c;
}

bool isThunk(uintptr_t rva) {
    uint8_t b = 0;
    return mem::readBytes(mem::moduleBase() + rva, &b, 1) && b == 0xe9;  // jmp rel32
}

// MyGUI::UString on our stack (its real size is well under this), built and destroyed by MyGUI.
struct UStringBox {
    alignas(16) uint8_t storage[256] = {};
    explicit UStringBox(const char* text) { calls().ustring(storage, text); }
    ~UStringBox() { calls().ustringDtor(storage); }
};

bool addTab(void* tabControl, MultiplayerOptions& options) {
    const MyGuiCalls& c = calls();
    const uintptr_t base = mem::moduleBase();
    UStringBox name(kTabName);
    void* emptyAny[2] = {};  // MyGUI::Any is one pointer; passed by value = by address, the callee owns it
    c.anyEmpty(emptyAny, c.anyNull);
    void* tab = c.addItem(tabControl, name.storage, emptyAny);
    if (!tab) return false;
    mygui::Vc10String panelName(kTabName);
    void* panel = reinterpret_cast<CreateDatapanelFn>(base + kCreateDatapanel)(base + kForgottenGui, &panelName, tab, true);
    if (!panel) return false;
    // Kenshi sets up each of its option panels right after making it: vtable + 0xc0 gives the panel
    // its id (every line of the panel then passes that same id), vtable + 0xe0 its line height.
    {
        void** vt = *reinterpret_cast<void***>(panel);
        reinterpret_cast<void (*)(void*, int)>(vt[0xc0 / 8])(panel, kPanelId);
        reinterpret_cast<void (*)(void*, float)>(vt[0xe0 / 8])(panel, kLineHeight);
    }
    // Lines grouped by category, like Kenshi's own tabs (title line, then its settings, then a gap).
    auto slider = [&](const char* text, float min, float max, float* value) {
        mygui::Vc10String label(text);
        reinterpret_cast<SetLineSliderFn>(base + kSetLineSlider)(panel, &label, kPanelId, true, min, max, value);
    };
    auto checkbox = [&](const char* text, bool* value) {
        mygui::Vc10String label(text);
        reinterpret_cast<SetLineCheckboxFn>(base + kSetLineCheckbox)(panel, &label, value, kPanelId);
    };
    auto gap = [&] { reinterpret_cast<AddSpaceFn>(base + kAddSpace)(panel, kPanelId, 10.0f); };

    // Grouped by category (a title line through setLine left the whole panel empty).
    slider("Chat - box opacity", 20.0f, 100.0f, &options.chatOpacity);
    slider("Chat - message fade time (s)", 5.0f, 120.0f, &options.chatFadeSec);
    gap();
    checkbox("Players - show names", &options.showNames);
    slider("Players - name tag distance", 50.0f, 2000.0f, &options.nameRange);
    gap();
    checkbox("Updates - install new versions automatically", &options.autoUpdate);
    void* panelAny[2] = {};
    reinterpret_cast<MakePanelAnyFn>(base + kMakePanelAny)(panelAny, &panel);
    c.setItemData(tabControl, tab, panelAny);
    return true;
}

bool guardedAddTab(void* tabControl, MultiplayerOptions& options) {
    __try {
        return addTab(tabControl, options);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void ensureOptionsTab(MultiplayerOptions& options) {
    if (!mygui::available() || !calls().ok) return;
    mygui::Widget* tabs = mygui::findShownBySuffix("_OptionsTab", 12);
    if (!tabs) return;
    static bool checked = false, usable = false;
    if (!checked) {
        checked = true;
        usable = isThunk(kCreateDatapanel) && isThunk(kSetLineSlider) && isThunk(kMakePanelAny) &&
                 isThunk(kSetLineCheckbox) && isThunk(kSetLine) && isThunk(kAddSpace);
        if (!usable) log("options: unexpected Kenshi code, no Multiplayer tab");
    }
    if (!usable) return;
    {
        UStringBox name(kTabName);
        if (calls().findItem(tabs, name.storage)) return;  // already there
    }
    bool ok = guardedAddTab(tabs, options);
    log("options: Multiplayer tab %s", ok ? "added" : "FAILED");
}

}  // namespace wok
