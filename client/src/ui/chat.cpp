#include "chat.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <string>

namespace wok {

namespace {
constexpr size_t kShownLines = 8;
constexpr float kTop = 0.29f;  // above Kenshi's bottom-left panels (relative to the screen)
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

void Chat::open() {
    mygui::destroy(mygui::findWidget(kInput));
    mygui::Widget* in =
        mygui::createRoot("EditBox", "Kenshi_EditBox", {0.01f, kTop + 0.024f * kShownLines, 0.40f, 0.03f}, "Popup", kInput);
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
    size_t shown = 0;
    for (size_t i = 0; i < kShownLines; ++i) {
        // Newest at the bottom: line i shows message (size - kShownLines + i).
        const size_t idx = i + lines_.size();
        const Line* line = idx >= kShownLines ? &lines_[idx - kShownLines] : nullptr;
        const bool visible = line && (typing_ || nowSec - line->time < kFadeSec);
        mygui::Widget* w = mygui::findWidget(lineName(i));
        if (!visible) {
            mygui::destroy(w);
            continue;
        }
        if (!w) {
            w = mygui::createRoot("TextBox", "Kenshi_TextboxStandardText", {0.01f, kTop + 0.024f * i, 0.45f, 0.024f},
                                  "Popup", lineName(i));
            if (!w) continue;
            mygui::setProperty(w, "TextShadow", "true");
            mygui::setProperty(w, "TextColour", "1 1 1");
            mygui::setProperty(w, "NeedMouse", "false");
        }
        mygui::setProperty(w, "Caption", line->text);
        ++shown;
    }
    drawnCount_ = shown;
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
