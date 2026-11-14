#include "chat.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

namespace wok {

namespace {
constexpr size_t kMaxLines = 40;     // messages kept
constexpr double kFadeSec = 20.0;    // a message stays this long unless the input is open
constexpr int kPad = 8, kInputH = 28, kLineH = 20, kGrip = 16;
constexpr int kMinW = 300, kMinH = 110;
constexpr float kBottom = 0.53f;     // default: the frame's bottom at 53% of the height
constexpr const char* kBox = "WokChatBox";
constexpr const char* kInput = "WokChatInput";
constexpr const char* kGripName = "WokChatGrip";
std::string lineName(size_t i) { return "WokChatLine" + std::to_string(i); }

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

HWND gameWindow() {
    HWND found = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            *reinterpret_cast<HWND*>(lp) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

bool gameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Size of the game window's client area (MyGUI pixels match it).
void gameViewSize(int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    RECT r;
    if (HWND hwnd = gameWindow(); hwnd && GetClientRect(hwnd, &r) && r.right > 0 && r.bottom > 0) {
        w = r.right;
        h = r.bottom;
    }
}

// Mouse position in the game window's client pixels.
bool mouseInGame(int& x, int& y) {
    POINT p;
    HWND hwnd = gameWindow();
    if (!hwnd || !GetCursorPos(&p) || !ScreenToClient(hwnd, &p)) return false;
    x = p.x;
    y = p.y;
    return true;
}
}  // namespace

size_t Chat::rows() const {
    int n = (boxH_ - 3 * kPad - kInputH) / kLineH;
    return static_cast<size_t>(std::clamp(n, 2, static_cast<int>(kMaxLines)));
}

void Chat::setSize(int width, int height) {
    int vw = 0, vh = 0;
    gameViewSize(vw, vh);
    boxW_ = std::clamp(width, kMinW, std::max(kMinW, vw - 20));
    boxH_ = std::clamp(height, kMinH, std::max(kMinH, vh - 20));
}

bool Chat::takeResized() {
    bool r = resized_;
    resized_ = false;
    return r;
}

void Chat::onMessage(const ChatMessage& m, double nowSec) {
    std::string text = (m.channel == ChatChannel::Local ? "[Local] " : "") + m.name + ": " + m.text;
    lines_.push_back({std::move(text), nowSec});
    while (lines_.size() > kMaxLines) lines_.pop_front();
    lastDrawn_ = -1;  // force a redraw
}

namespace {
// A rectangle inside the frame, in pixels from its top-left corner (children use relative coords).
mygui::FloatCoord inBox(int boxW, int boxH, float x, float y, float w, float h) {
    return {x / boxW, y / boxH, w / boxW, h / boxH};
}
}  // namespace

void Chat::rebuild() {
    std::string draft;
    if (mygui::Widget* in = mygui::findWidget(kInput)) {  // keep what is being typed
        const char* raw = mygui::editText(in);
        draft = raw ? raw : "";
    }
    mygui::destroy(mygui::findWidget(kBox));
    if (typing_) {
        typing_ = false;
        open();
        if (mygui::Widget* in = mygui::findWidget(kInput); in && !draft.empty()) mygui::injectText(in, draft);
    }
    lastDrawn_ = -1;
}

// The chat frame (Kenshi's beige info panel look); lines, input and resize grip live inside it.
static mygui::Widget* chatBox(int w, int h, int& left, int& top, bool create) {
    mygui::Widget* box = mygui::findWidget(kBox);
    if (box || !create) return box;
    if (left < 0) {
        int vw = 0, vh = 0;
        gameViewSize(vw, vh);
        left = vw / 100;
        top = static_cast<int>(vh * kBottom) - h;
    }
    box = mygui::createRoot("Widget", "Kenshi_GenericTextBoxSkin", {0.01f, 0.3f, 0.4f, 0.2f}, "Popup", kBox);
    if (!box) return nullptr;
    mygui::setCoord(box, left, top, w, h);
    static bool logged = false;
    if (!logged) {
        logged = true;
        int vw = 0, vh = 0;
        gameViewSize(vw, vh);
        log("chat: frame %d x %d at (%d, %d), game view %d x %d", w, h, left, top, vw, vh);
    }
    // Resize grip: bottom-right corner, drag to resize.
    if (mygui::Widget* grip = mygui::createChild(box, "Widget", "WhiteSkin",
                                                 inBox(w, h, float(w - kGrip - 2), float(h - kGrip - 2), kGrip, kGrip),
                                                 kGripName)) {
        mygui::setProperty(grip, "Colour", "0.35 0.30 0.22");
        mygui::setProperty(grip, "Alpha", "0.8");
        mygui::setProperty(grip, "Pointer", "size_left");
    }
    return box;
}

void Chat::open() {
    mygui::Widget* box = chatBox(boxW_, boxH_, boxLeft_, boxTop_, true);
    if (!box) return;
    mygui::destroy(mygui::findWidget(kInput));
    const int inputY = boxH_ - kPad - kInputH;
    mygui::Widget* in = mygui::createChild(box, "EditBox", "Kenshi_EditBox",
                                           inBox(boxW_, boxH_, kPad, float(inputY), float(boxW_ - 2 * kPad - kGrip - 4),
                                                 kInputH),
                                           kInput);
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
    const size_t n = rows();
    auto visibleAt = [&](size_t i) -> const Line* {
        // Newest at the bottom: row i shows message (size - n + i).
        const size_t idx = i + lines_.size();
        const Line* line = idx >= n ? &lines_[idx - n] : nullptr;
        return line && (typing_ || dragging_ || nowSec - line->time < kFadeSec) ? line : nullptr;
    };
    size_t shown = 0;
    for (size_t i = 0; i < n; ++i)
        if (visibleAt(i)) ++shown;
    drawnCount_ = shown;
    if (shown == 0 && !typing_ && !dragging_) {  // nothing to show: no frame either
        mygui::destroy(chatBox(boxW_, boxH_, boxLeft_, boxTop_, false));
        return;
    }
    mygui::Widget* box = chatBox(boxW_, boxH_, boxLeft_, boxTop_, true);
    if (!box) return;
    mygui::setProperty(box, "Alpha", std::to_string(opacity_ / 100.0f));
    const int messagesBottom = boxH_ - 2 * kPad - kInputH;  // rows end above the input line
    for (size_t i = 0; i < kMaxLines; ++i) {
        const Line* line = i < n ? visibleAt(i) : nullptr;
        mygui::Widget* w = mygui::findWidget(lineName(i));
        if (!line) {
            mygui::destroy(w);
            continue;
        }
        if (!w) {
            const float y = float(messagesBottom - static_cast<int>(n - i) * kLineH);
            w = mygui::createChild(box, "TextBox", "Kenshi_TextboxStandardText",
                                   inBox(boxW_, boxH_, kPad, y, float(boxW_ - 2 * kPad), kLineH), lineName(i));
            if (!w) continue;
            mygui::setProperty(w, "NeedMouse", "false");
        }
        mygui::setProperty(w, "Caption", line->text);
    }
}

void Chat::dragCorner() {
    const bool down = keyDown(VK_LBUTTON);
    // Pressed on the grip: checked against the frame's known pixel rectangle (MyGUI's mouse focus
    // does not reach a child of a frame that ignores the mouse).
    if (down && !mouseWasDown_ && boxLeft_ >= 0 && mygui::findWidget(kBox)) {
        int mx = 0, my = 0;
        const int right = boxLeft_ + boxW_, bottom = boxTop_ + boxH_;
        if (mouseInGame(mx, my) && mx >= right - kGrip - 6 && mx <= right + 2 && my >= bottom - kGrip - 6 &&
            my <= bottom + 2)
            dragging_ = true;
    }
    if (dragging_) {
        int mx = 0, my = 0;
        if (mouseInGame(mx, my) && boxLeft_ >= 0) {
            // The top-left corner stays; the bottom-right corner follows the mouse.
            int oldW = boxW_, oldH = boxH_;
            setSize(mx - boxLeft_ + kGrip / 2, my - boxTop_ + kGrip / 2);
            if (boxW_ != oldW || boxH_ != oldH) rebuild();
        }
        if (!down) {
            dragging_ = false;
            resized_ = true;
            lastDrawn_ = -1;
            log("chat: frame resized to %d x %d", boxW_, boxH_);
        }
    }
    mouseWasDown_ = down;
}

void Chat::update(double nowSec, bool active, bool hidden) {
    if (!mygui::available()) return;
    if (active && hidden) {  // pause menu open: put the chat away until it closes
        if (typing_) close(false);
        dragging_ = false;
        mygui::destroy(mygui::findWidget(kBox));
        drawnCount_ = 0;
        lastDrawn_ = -1;
        enterWasDown_ = true;
        return;
    }
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
    dragCorner();
    // Redraw when something changed or once a second (fading).
    if (opacity_ != appliedOpacity_) {
        appliedOpacity_ = opacity_;
        lastDrawn_ = -1;
    }
    if (lastDrawn_ < 0 || nowSec - lastDrawn_ >= 1.0) {
        redraw(nowSec);
        lastDrawn_ = nowSec;
    }
}

}  // namespace wok
