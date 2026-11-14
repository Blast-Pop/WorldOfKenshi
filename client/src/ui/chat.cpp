#include "chat.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <string>

namespace wok {

namespace {
constexpr size_t kShownLines = 8;
constexpr double kFadeSec = 20.0;  // a message stays this long unless the input is open
constexpr const char* kInput = "WokChatInput";
std::string lineName(size_t i) { return "WokChatLine" + std::to_string(i); }

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
bool gameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
}  // namespace

void Chat::onMessage(const ChatMessage& m, double nowSec) {
    std::string text = (m.channel == ChatChannel::Local ? "[Local] " : "") + m.name + ": " + m.text;
    lines_.push_back({std::move(text), nowSec});
    while (lines_.size() > kShownLines) lines_.pop_front();
    lastDrawn_ = -1;  // force a redraw
}

namespace {
constexpr const char* kBox = "WokChatBox";
// The frame in pixels (Aeglen: 775 x 210), its bottom where the old frame's was (53% of the height).
constexpr int kBoxWpx = 775, kBoxHpx = 210, kPadPx = 8, kInputHpx = 28;
constexpr float kBottom = 0.53f;
constexpr float kLineHpx = float(kBoxHpx - 3 * kPadPx - kInputHpx) / kShownLines;

// Size of the game window's client area (MyGUI pixels match it).
void gameViewSize(int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    struct Find {
        HWND hwnd = nullptr;
    } f;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            reinterpret_cast<Find*>(lp)->hwnd = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&f));
    RECT r;
    if (f.hwnd && GetClientRect(f.hwnd, &r) && r.right > 0 && r.bottom > 0) {
        w = r.right;
        h = r.bottom;
    }
}

// The chat frame (Kenshi's beige info panel look); lines and the input line live inside it.
mygui::Widget* chatBox(bool create) {
    mygui::Widget* box = mygui::findWidget(kBox);
    if (box || !create) return box;
    int vw = 0, vh = 0;
    gameViewSize(vw, vh);
    box = mygui::createRoot("Widget", "Kenshi_GenericTextBoxSkin", {0.01f, 0.3f, 0.4f, 0.2f}, "Popup", kBox);
    if (!box) return nullptr;
    mygui::setCoord(box, vw / 100, static_cast<int>(vh * kBottom) - kBoxHpx, kBoxWpx, kBoxHpx);
    mygui::setProperty(box, "NeedMouse", "false");
    return box;
}

// A rectangle inside the box, in pixels from its top-left corner (children use relative coords).
mygui::FloatCoord inBox(float x, float y, float w, float h) {
    return {x / kBoxWpx, y / kBoxHpx, w / kBoxWpx, h / kBoxHpx};
}
}  // namespace

void Chat::open() {
    mygui::Widget* box = chatBox(true);
    if (!box) return;
    mygui::destroy(mygui::findWidget(kInput));
    mygui::Widget* in = mygui::createChild(box, "EditBox", "Kenshi_EditBox",
                                           inBox(kPadPx, kPadPx * 2 + kLineHpx * kShownLines, kBoxWpx - 2 * kPadPx, kInputHpx), kInput);
    if (!in) return;
    mygui::setProperty(in, "MaxTextLength", "200");
    mygui::setKeyFocus(in);
    typing_ = true;
    lastDrawn_ = -1;
}

void Chat::close(bool sendIt) {
    mygui::Widget* in = mygui::findWidget(kInput);
    if (in && sendIt) {
        const char* raw = mygui::editText(in);
        std::string text = raw ? raw : "";
        ChatSend m;
        if (text.rfind("/l ", 0) == 0) {
            m.channel = ChatChannel::Local;
            text.erase(0, 3);
        } else if (text.rfind("/g ", 0) == 0) {
            text.erase(0, 3);
        }
        if (text.find_first_not_of(' ') != std::string::npos) {
            m.text = text.substr(0, kMaxChatBytes);
            send_(m);
        }
    }
    mygui::setKeyFocus(nullptr);
    mygui::destroy(in);
    typing_ = false;
    lastDrawn_ = -1;
}

void Chat::redraw(double nowSec) {
    auto visibleAt = [&](size_t i) -> const Line* {
        // Newest at the bottom: row i shows message (size - kShownLines + i).
        const size_t idx = i + lines_.size();
        const Line* line = idx >= kShownLines ? &lines_[idx - kShownLines] : nullptr;
        return line && (typing_ || nowSec - line->time < kFadeSec) ? line : nullptr;
    };
    size_t shown = 0;
    for (size_t i = 0; i < kShownLines; ++i)
        if (visibleAt(i)) ++shown;
    drawnCount_ = shown;
    if (shown == 0 && !typing_) {  // nothing to show: no frame either
        mygui::destroy(chatBox(false));
        return;
    }
    mygui::Widget* box = chatBox(true);
    if (!box) return;
    for (size_t i = 0; i < kShownLines; ++i) {
        const Line* line = visibleAt(i);
        mygui::Widget* w = mygui::findWidget(lineName(i));
        if (!line) {
            mygui::destroy(w);
            continue;
        }
        if (!w) {
            w = mygui::createChild(box, "TextBox", "Kenshi_TextboxStandardText",
                                   inBox(kPadPx, kPadPx + kLineHpx * i, kBoxWpx - 2 * kPadPx, kLineHpx), lineName(i));
            if (!w) continue;
            mygui::setProperty(w, "NeedMouse", "false");
        }
        mygui::setProperty(w, "Caption", line->text);
    }
}

void Chat::update(double nowSec, bool active) {
    if (!mygui::available()) return;
    if (!active) {
        if (typing_) close(false);
        if (drawnCount_) {
            lines_.clear();
            redraw(nowSec);
        }
        enterWasDown_ = true;
        return;
    }
    const bool enter = gameHasFocus() && keyDown(VK_RETURN);
    const bool esc = gameHasFocus() && keyDown(VK_ESCAPE);
    if (enter && !enterWasDown_) {
        if (typing_)
            close(true);
        else if (!mygui::keyFocus())  // not while typing in one of Kenshi's own fields
            open();
    } else if (esc && !escWasDown_ && typing_) {
        close(false);
    }
    enterWasDown_ = enter;
    escWasDown_ = esc;
    if (typing_ && mygui::keyFocus() != mygui::findWidget(kInput)) close(false);  // focus taken elsewhere
    // Redraw when something changed or once a second (fading).
    if (lastDrawn_ < 0 || nowSec - lastDrawn_ >= 1.0) {
        redraw(nowSec);
        lastDrawn_ = nowSec;
    }
}

}  // namespace wok
