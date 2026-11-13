#pragma once
// Minimal, ABI-compatible access to Kenshi's MyGUI (MyGUIEngine_x64.dll, built with MSVC 2010).
// Only functions taking `const std::string&`, PODs and pointers are used. Strings are passed as
// a hand-made VC10 std::string (32 bytes: 16-byte SSO buffer, size, capacity - see docs/re/mygui.md);
// MyGUI copies them, so ownership never crosses the boundary.

#include <cstddef>
#include <string_view>

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
const char* editText(Widget* editBox);  // UTF-8, valid until the next MyGUI call

}  // namespace wok::mygui
