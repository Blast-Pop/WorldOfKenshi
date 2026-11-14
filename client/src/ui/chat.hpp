#pragma once
// In-game chat (spec 7.8): the last messages at the bottom left, Enter to type, "/l " for local
// (players nearby), global otherwise. Our own MyGUI widgets.

#include <wok/protocol.hpp>

#include <deque>
#include <functional>
#include <string>

namespace wok {

class Chat {
public:
    explicit Chat(std::function<void(const ChatSend&)> send) : send_(std::move(send)) {}
    void onMessage(const ChatMessage& m, double nowSec);
    // Every frame. `active`: in a server world (otherwise the chat is hidden and closed).
    void update(double nowSec, bool active);
    bool typing() const { return typing_; }

private:
    struct Line {
        std::string text;
        double time;
    };
    void open();
    void close(bool sendIt);
    void redraw(double nowSec);

    std::function<void(const ChatSend&)> send_;
    std::deque<Line> lines_;
    bool typing_ = false;
    bool enterWasDown_ = true;  // ignore an Enter already held when the world appears
    bool escWasDown_ = false;
    double lastDrawn_ = -1;
    size_t drawnCount_ = 0;
};

}  // namespace wok
