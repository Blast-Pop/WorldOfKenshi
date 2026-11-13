#include "mygui_abi.hpp"

#include "../game/game_memory.hpp"
#include "../log.hpp"

#include <windows.h>

#include <cstring>

namespace wok::mygui {

Vc10String::Vc10String(std::string_view s) : size(s.size()) {
    if (s.size() < 16) {
        std::memcpy(buf, s.data(), s.size());
        buf[s.size()] = '\0';
        capacity = 15;
    } else {
        ptr = new char[s.size() + 1];
        std::memcpy(ptr, s.data(), s.size());
        ptr[s.size()] = '\0';
        capacity = s.size();
    }
}

Vc10String::~Vc10String() {
    if (capacity >= 16) delete[] ptr;
}

namespace {

using S = Vc10String;
struct Api {
    void* (*getGui)();
    void* (*getInput)();
    Widget* (*findWidgetT)(void* gui, const S* name, bool throwIfMissing);
    Widget* (*getParent)(Widget* w);
    Widget* (*widgetCreateRealT)(Widget* parent, const S* type, const S* skin, const FloatCoord* coord, int align,
                                 const S* name);
    Widget* (*guiCreateRealT)(void* gui, const S* type, const S* skin, const FloatCoord* coord, int align,
                              const S* layer, const S* name);
    void (*setProperty)(Widget* w, const S* key, const S* value);
    void (*destroyWidget)(void* gui, Widget* w);
    Widget* (*getMouseFocusWidget)(void* input);
    const void* (*editGetCaption)(Widget* edit);
    const char* (*ustringC)(const void* ustring);
    const S* (*getName)(Widget* w);
    size_t (*getChildCount)(Widget* w);
    Widget* (*getChildAt)(Widget* w, size_t index);
} g_api{};

// Gui keeps its root widgets in a std::vector<Widget*> at +0x28 (begin) / +0x30 (end): seen in
// Gui::findWidgetT, which iterates exactly that range (docs/re/mygui.md).
constexpr uintptr_t kGuiRootsBegin = 0x28;
constexpr uintptr_t kGuiRootsEnd = 0x30;

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Widget* searchChildren(Widget* w, std::string_view suffix, int depth) {
    if (!w) return nullptr;
    if (endsWith(nameOf(w), suffix)) return w;
    if (depth == 0) return nullptr;
    size_t n = g_api.getChildCount(w);
    for (size_t i = 0; i < n && i < 512; ++i)
        if (Widget* hit = searchChildren(g_api.getChildAt(w, i), suffix, depth - 1)) return hit;
    return nullptr;
}
bool g_ok = false;

template <class F>
bool load(HMODULE m, F& fn, const char* name) {
    fn = reinterpret_cast<F>(GetProcAddress(m, name));
    if (!fn) log("mygui: missing export %s", name);
    return fn != nullptr;
}

}  // namespace

bool init() {
    HMODULE m = GetModuleHandleA("MyGUIEngine_x64.dll");
    if (!m) {
        log("mygui: MyGUIEngine_x64.dll not loaded");
        return false;
    }
    bool ok = true;
    ok &= load(m, g_api.getGui, "?getInstancePtr@?$Singleton@VGui@MyGUI@@@MyGUI@@SAPEAVGui@2@XZ");
    ok &= load(m, g_api.getInput, "?getInstancePtr@?$Singleton@VInputManager@MyGUI@@@MyGUI@@SAPEAVInputManager@2@XZ");
    ok &= load(m, g_api.findWidgetT,
               "?findWidgetT@Gui@MyGUI@@QEAAPEAVWidget@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@"
               "std@@_N@Z");
    ok &= load(m, g_api.getParent, "?getParent@Widget@MyGUI@@QEBAPEAV12@XZ");
    ok &= load(m, g_api.widgetCreateRealT,
               "?createWidgetRealT@Widget@MyGUI@@QEAAPEAV12@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@"
               "2@@std@@0AEBU?$TCoord@M@types@2@UAlign@2@0@Z");
    ok &= load(m, g_api.guiCreateRealT,
               "?createWidgetRealT@Gui@MyGUI@@QEAAPEAVWidget@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$"
               "allocator@D@2@@std@@0AEBU?$TCoord@M@types@2@UAlign@2@00@Z");
    ok &= load(m, g_api.setProperty,
               "?setProperty@Widget@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
    ok &= load(m, g_api.destroyWidget, "?destroyWidget@Gui@MyGUI@@QEAAXPEAVWidget@2@@Z");
    ok &= load(m, g_api.getMouseFocusWidget, "?getMouseFocusWidget@InputManager@MyGUI@@QEBAPEAVWidget@2@XZ");
    ok &= load(m, g_api.editGetCaption, "?getCaption@EditBox@MyGUI@@UEAAAEBVUString@2@XZ");
    ok &= load(m, g_api.ustringC, "?asUTF8_c_str@UString@MyGUI@@QEBAPEBDXZ");
    ok &= load(m, g_api.getName,
               "?getName@Widget@MyGUI@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ");
    ok &= load(m, g_api.getChildCount, "?getChildCount@Widget@MyGUI@@QEAA_KXZ");
    ok &= load(m, g_api.getChildAt, "?getChildAt@Widget@MyGUI@@QEAAPEAV12@_K@Z");
    g_ok = ok;
    log("mygui: %s", ok ? "ready" : "disabled");
    return ok;
}

bool available() { return g_ok && g_api.getGui() != nullptr; }

Widget* findWidget(std::string_view name) {
    void* gui = g_api.getGui();
    if (!gui) return nullptr;
    S n(name);
    return g_api.findWidgetT(gui, &n, false);
}

Widget* parentOf(Widget* w) { return w ? g_api.getParent(w) : nullptr; }

Widget* createChild(Widget* parent, std::string_view type, std::string_view skin, FloatCoord coord,
                    std::string_view name) {
    S t(type), s(skin), n(name);
    return g_api.widgetCreateRealT(parent, &t, &s, &coord, AlignDefault, &n);
}

Widget* createRoot(std::string_view type, std::string_view skin, FloatCoord coord, std::string_view layer,
                   std::string_view name) {
    void* gui = g_api.getGui();
    if (!gui) return nullptr;
    S t(type), s(skin), l(layer), n(name);
    return g_api.guiCreateRealT(gui, &t, &s, &coord, AlignDefault, &l, &n);
}

void setProperty(Widget* w, std::string_view key, std::string_view value) {
    if (!w) return;
    S k(key), v(value);
    g_api.setProperty(w, &k, &v);
}

void destroy(Widget* w) {
    void* gui = g_api.getGui();
    if (gui && w) g_api.destroyWidget(gui, w);
}

Widget* mouseFocus() {
    void* input = g_api.getInput();
    return input ? g_api.getMouseFocusWidget(input) : nullptr;
}

Widget* findBySuffix(std::string_view suffix, int maxDepth) {
    auto gui = reinterpret_cast<uintptr_t>(g_api.getGui());
    uintptr_t begin = 0, end = 0;
    if (!gui || !mem::readPtr(gui + kGuiRootsBegin, begin) || !mem::readPtr(gui + kGuiRootsEnd, end)) return nullptr;
    if (end < begin || end - begin > 8 * 4096) return nullptr;
    for (uintptr_t it = begin; it < end; it += 8) {
        uintptr_t root = 0;
        if (!mem::readPtr(it, root)) continue;
        if (Widget* hit = searchChildren(reinterpret_cast<Widget*>(root), suffix, maxDepth)) return hit;
    }
    return nullptr;
}

std::string nameOf(Widget* w) {
    if (!w) return {};
    const S* s = g_api.getName(w);
    if (!s || s->size > 4096) return {};
    return std::string(s->capacity >= 16 ? s->ptr : s->buf, s->size);
}

const char* editText(Widget* editBox) {
    if (!editBox) return "";
    const void* u = g_api.editGetCaption(editBox);
    return u ? g_api.ustringC(u) : "";
}

}  // namespace wok::mygui
