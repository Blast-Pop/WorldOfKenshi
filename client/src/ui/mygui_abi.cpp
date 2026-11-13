#include "mygui_abi.hpp"

#include "../game/game_memory.hpp"
#include "../log.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

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
    void (*setKeyFocus)(void* input, Widget* w);
    void (*resetKeyFocus)(void* input);
    bool (*inheritedVisible)(Widget* w);
    Widget* (*getKeyFocus)(void* input);
    bool (*injectKeyPress)(void* input, int keyCode, unsigned int ch);
    bool (*injectKeyRelease)(void* input, int keyCode);
    const char* (*ustringC)(const void* ustring);
    const S* (*getName)(Widget* w);
    size_t (*getChildCount)(Widget* w);
    Widget* (*getChildAt)(Widget* w, size_t index);
    void (*riseClick)(void* widgetInput);
    void* (*getLayoutManager)();
    void (*setCoordFn)(Widget* w, int left, int top, int width, int height);
    // Returns std::vector<Widget*> by value: hidden result buffer in the second register.
    void* (*loadLayoutFn)(void* manager, void* result, const S* file, const S* prefix, Widget* parent);
} g_api{};

// MyGUI::Widget derives from several classes; its WidgetInput base sits at +0x80 (Widget's RTTI
// class hierarchy in MyGUIEngine_x64.dll, docs/re/mygui.md).
constexpr uintptr_t kWidgetInputOffset = 0x80;

// Gui keeps its root widgets in a std::vector<Widget*> at +0x28 (begin) / +0x30 (end): seen in
// Gui::findWidgetT, which iterates exactly that range (docs/re/mygui.md).
constexpr uintptr_t kGuiRootsBegin = 0x28;
constexpr uintptr_t kGuiRootsEnd = 0x30;

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

constexpr const char* kEditBoxRtti = ".?AVEditBox@MyGUI@@";

Widget* searchChildren(Widget* w, std::string_view suffix, int depth, std::string_view exclude = {},
                       const char* rtti = nullptr) {
    if (!w) return nullptr;
    std::string n = nameOf(w);
    if (endsWith(n, suffix) && (exclude.empty() || n.compare(0, exclude.size(), exclude) != 0) &&
        (!rtti || mem::isA(reinterpret_cast<uintptr_t>(w), rtti)))
        return w;
    if (depth == 0) return nullptr;
    size_t k = g_api.getChildCount(w);
    for (size_t i = 0; i < k && i < 512; ++i)
        if (Widget* hit = searchChildren(g_api.getChildAt(w, i), suffix, depth - 1, exclude, rtti)) return hit;
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
    load(m, g_api.setKeyFocus, "?setKeyFocusWidget@InputManager@MyGUI@@QEAAXPEAVWidget@2@@Z");
    load(m, g_api.resetKeyFocus, "?resetKeyFocusWidget@InputManager@MyGUI@@QEAAXXZ");
    load(m, g_api.inheritedVisible, "?getInheritedVisible@Widget@MyGUI@@QEBA_NXZ");
    load(m, g_api.getKeyFocus, "?getKeyFocusWidget@InputManager@MyGUI@@QEBAPEAVWidget@2@XZ");
    load(m, g_api.injectKeyPress, "?injectKeyPress@InputManager@MyGUI@@QEAA_NUKeyCode@2@I@Z");
    load(m, g_api.injectKeyRelease, "?injectKeyRelease@InputManager@MyGUI@@QEAA_NUKeyCode@2@@Z");
    ok &= load(m, g_api.ustringC, "?asUTF8_c_str@UString@MyGUI@@QEBAPEBDXZ");
    ok &= load(m, g_api.getName,
               "?getName@Widget@MyGUI@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ");
    ok &= load(m, g_api.getChildCount, "?getChildCount@Widget@MyGUI@@QEAA_KXZ");
    ok &= load(m, g_api.getChildAt, "?getChildAt@Widget@MyGUI@@QEAAPEAV12@_K@Z");
    ok &= load(m, g_api.riseClick, "?_riseMouseButtonClick@WidgetInput@MyGUI@@QEAAXXZ");
    // Optional: only the loading screen needs these.
    load(m, g_api.setCoordFn, "?setCoord@Widget@MyGUI@@QEAAXHHHH@Z");
    load(m, g_api.getLayoutManager, "?getInstancePtr@?$Singleton@VLayoutManager@MyGUI@@@MyGUI@@SAPEAVLayoutManager@2@XZ");
    load(m, g_api.loadLayoutFn,
         "?loadLayout@LayoutManager@MyGUI@@QEAA?AV?$vector@PEAVWidget@MyGUI@@V?$allocator@PEAVWidget@MyGUI@@@std@@@std@@"
         "AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@4@0PEAVWidget@2@@Z");
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

namespace {
void logMatches(Widget* w, std::string_view suffix, int depth, int& count) {
    if (!w || count > 200) return;
    std::string n = nameOf(w);
    if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
        log("ui:   %s", n.c_str());
        ++count;
    }
    if (depth == 0) return;
    size_t k = g_api.getChildCount(w);
    for (size_t i = 0; i < k && i < 512; ++i) logMatches(g_api.getChildAt(w, i), suffix, depth - 1, count);
}
}  // namespace

void logWidgetsWithSuffix(std::string_view suffix, int maxDepth) {
    auto gui = reinterpret_cast<uintptr_t>(g_api.getGui());
    uintptr_t begin = 0, end = 0;
    if (!gui || !mem::readPtr(gui + kGuiRootsBegin, begin) || !mem::readPtr(gui + kGuiRootsEnd, end)) return;
    int count = 0;
    for (uintptr_t it = begin; it < end && end - begin <= 8 * 4096; it += 8) {
        uintptr_t root = 0;
        if (mem::readPtr(it, root)) logMatches(reinterpret_cast<Widget*>(root), suffix, maxDepth, count);
    }
}


Widget* findBySuffixExcept(std::string_view suffix, std::string_view excludedPrefix, int maxDepth) {
    auto gui = reinterpret_cast<uintptr_t>(g_api.getGui());
    uintptr_t begin = 0, end = 0;
    if (!gui || !mem::readPtr(gui + kGuiRootsBegin, begin) || !mem::readPtr(gui + kGuiRootsEnd, end)) return nullptr;
    if (end < begin || end - begin > 8 * 4096) return nullptr;
    for (uintptr_t it = begin; it < end; it += 8) {
        uintptr_t root = 0;
        if (!mem::readPtr(it, root)) continue;
        if (Widget* hit = searchChildren(reinterpret_cast<Widget*>(root), suffix, maxDepth, excludedPrefix)) return hit;
    }
    return nullptr;
}


namespace {
void collectEditBoxes(Widget* w, std::string_view suffix, int depth, std::vector<Widget*>& out) {
    if (!w || out.size() > 64) return;
    if (endsWith(nameOf(w), suffix) && mem::isA(reinterpret_cast<uintptr_t>(w), kEditBoxRtti)) out.push_back(w);
    if (depth == 0) return;
    size_t k = g_api.getChildCount(w);
    for (size_t i = 0; i < k && i < 512; ++i) collectEditBoxes(g_api.getChildAt(w, i), suffix, depth - 1, out);
}
}  // namespace

std::vector<Widget*> findEditBoxesBySuffix(std::string_view suffix, int maxDepth) {
    std::vector<Widget*> out;
    auto gui = reinterpret_cast<uintptr_t>(g_api.getGui());
    uintptr_t begin = 0, end = 0;
    if (!gui || !mem::readPtr(gui + kGuiRootsBegin, begin) || !mem::readPtr(gui + kGuiRootsEnd, end)) return out;
    if (end < begin || end - begin > 8 * 4096) return out;
    for (uintptr_t it = begin; it < end; it += 8) {
        uintptr_t root = 0;
        if (mem::readPtr(it, root)) collectEditBoxes(reinterpret_cast<Widget*>(root), suffix, maxDepth, out);
    }
    return out;
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

void click(Widget* w) {
    if (w) g_api.riseClick(reinterpret_cast<char*>(w) + kWidgetInputOffset);
}

bool loadLayout(std::string_view file, std::string_view prefix, Widget* parent) {
    if (!g_api.getLayoutManager || !g_api.loadLayoutFn) return false;
    void* manager = g_api.getLayoutManager();
    if (!manager) return false;
    // Room for MSVC 2010's std::vector<Widget*> result; its small buffer is left to the game's heap
    // (a few bytes per load), we only need the widgets, found afterwards by name.
    alignas(16) static uint8_t result[64];
    std::memset(result, 0, sizeof(result));
    S f(file), p(prefix);
    g_api.loadLayoutFn(manager, result, &f, &p, parent);
    return true;
}

void setCoord(Widget* w, int left, int top, int width, int height) {
    if (w && g_api.setCoordFn) g_api.setCoordFn(w, left, top, width, height);
}

void setVisible(Widget* w, bool visible) { setProperty(w, "Visible", visible ? "true" : "false"); }

std::string nameOf(Widget* w) {
    if (!w) return {};
    const S* s = g_api.getName(w);
    if (!s || s->size > 4096) return {};
    return std::string(s->capacity >= 16 ? s->ptr : s->buf, s->size);
}

std::string injectText(Widget* editBox, std::string_view text) {
    void* input = g_api.getInput();
    if (!editBox || !input || !mem::isA(reinterpret_cast<uintptr_t>(editBox), kEditBoxRtti) || !g_api.setKeyFocus ||
        !g_api.injectKeyPress || !g_api.injectKeyRelease)
        return {};
    // MyGUI's edit key handler ignores everything while the box is static (+0x5ac) and characters
    // while it is read-only (+0x5a9); Kenshi keeps its editor name field locked until clicked.
    uint8_t flags[4] = {};
    mem::readBytes(reinterpret_cast<uintptr_t>(editBox) + 0x5a9, flags, 4);
    setProperty(editBox, "Static", "false");
    setProperty(editBox, "ReadOnly", "false");
    setProperty(editBox, "Caption", "");
    g_api.setKeyFocus(input, editBox);
    for (unsigned char c : text) {
        g_api.injectKeyPress(input, 0, c);
        g_api.injectKeyRelease(input, 0);
    }
    if (g_api.resetKeyFocus) g_api.resetKeyFocus(input);
    std::string now = editText(editBox);
    log("mygui: injected text (read-only %d, static %d) -> '%s'", flags[0], flags[3], now.c_str());
    return now;
}

bool isShown(Widget* w) { return w && g_api.inheritedVisible && g_api.inheritedVisible(w); }


const char* editText(Widget* editBox) {
    if (!editBox) return "";
    const void* u = g_api.editGetCaption(editBox);
    return u ? g_api.ustringC(u) : "";
}

}  // namespace wok::mygui
