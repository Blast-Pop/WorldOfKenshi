#pragma once
// Minimal, ABI-compatible access to Kenshi's MyGUI (MyGUIEngine_x64.dll, built with MSVC 2010).
// Only functions taking `const std::string&`, PODs and pointers are used. Strings are passed as
// a hand-made VC10 std::string (32 bytes: 16-byte SSO buffer, size, capacity - see docs/re/mygui.md);
// MyGUI copies them, so ownership never crosses the boundary.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wok::mygui {

struct Widget;  // opaque MyGUI::Widget*

struct Vc10String {
    union {
        char buf[16];
        char* ptr;
    };
    size_t size;
    size_t capacity;

    explicit Vc10String(std::string_view s);
    ~Vc10String();
    Vc10String(const Vc10String&) = delete;
    Vc10String& operator=(const Vc10String&) = delete;
};
static_assert(sizeof(Vc10String) == 32);

struct FloatCoord {
    float left, top, width, height;
};

enum Align : int { AlignDefault = 2 | 8 };  // Left | Top

bool init();  // resolves exports; false (and logged) if MyGUI is not usable
bool available();

Widget* findWidget(std::string_view name);
Widget* parentOf(Widget* w);
Widget* createChild(Widget* parent, std::string_view type, std::string_view skin, FloatCoord coord,
                    std::string_view name);
Widget* createRoot(std::string_view type, std::string_view skin, FloatCoord coord, std::string_view layer,
                   std::string_view name);
void setProperty(Widget* w, std::string_view key, std::string_view value);
void destroy(Widget* w);
Widget* mouseFocus();
bool isShown(Widget* w);  // visible, parents included
// Unlocks the edit box and feeds `text` through MyGUI's key input; returns the resulting text.
std::string injectText(Widget* editBox, std::string_view text);
std::string labelText(Widget* textBox);
// Presses (left button) the first row of a MyGUI ListBox whose text contains `containing`, like a
// mouse click. True when the list then has that row selected.
bool pressListItem(Widget* list, std::string_view containing);  // caption of a plain MyGUI TextBox, empty otherwise
const char* editText(Widget* editBox);  // UTF-8, valid until the next MyGUI call
std::string nameOf(Widget* w);          // full MyGUI name (layout prefix included)
// Kenshi loads layouts with a per-instance name prefix ("0,000,...,4B0_NewGameButton"), so its own
// widgets are found by name suffix, searching the root widgets down to `maxDepth` levels.
Widget* findBySuffix(std::string_view suffix, int maxDepth);
// Same, ignoring widgets whose name starts with `excludedPrefix` (e.g. our own copies).
Widget* findBySuffixExcept(std::string_view suffix, std::string_view excludedPrefix, int maxDepth);
// All MyGUI EditBox widgets (checked by RTTI) whose name ends with `suffix`.
std::vector<Widget*> findEditBoxesBySuffix(std::string_view suffix, int maxDepth);
// First visible widget whose name ends with `suffix` (layouts often keep hidden duplicates).
Widget* findShownBySuffix(std::string_view suffix, int maxDepth);
// Diagnostic: logs the full name of every widget whose name ends with `suffix`.
void logWidgetsWithSuffix(std::string_view suffix, int maxDepth);
// Fires the widget's click event exactly like a real mouse click (runs the game's own handler).
void click(Widget* w);
void setVisible(Widget* w, bool visible);
// Loads one of Kenshi's own layout files (e.g. "Kenshi_LoadingScreen.layout"); its widgets are
// named `prefix` + their layout name. Returns false if MyGUI could not load it.
bool loadLayout(std::string_view file, std::string_view prefix, Widget* parent = nullptr);
// Pixel coordinates (MyGUI view space), e.g. to make a root widget cover the whole game window.
void setCoord(Widget* w, int left, int top, int width, int height);

}  // namespace wok::mygui
