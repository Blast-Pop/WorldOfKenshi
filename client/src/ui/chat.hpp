#pragma once
// In-game chat (spec 7.8): recent messages in a Kenshi-style frame at the bottom left, Enter to type,
// "/l " for local (players nearby), global otherwise. The frame is resized by dragging its
// bottom-right corner. Our own MyGUI widgets.

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
    // `hidden`: a menu covers the game (pause menu): no frame, no typing, messages kept.
    void update(double nowSec, bool active, bool hidden = false);
    bool typing() const { return typing_; }
    void setOpacity(float percent) { opacity_ = percent; }
    // Frame size in pixels (saved by the caller when the player resizes it).
    void setSize(int width, int height);
    int width() const { return boxW_; }
    int height() const { return boxH_; }
    // True once after the player finished resizing (to save the new size).
    bool takeResized();

private:
    struct Line {
        std::string text;
        double time;
    };
    void open();
    void close(bool sendIt);
    void redraw(double nowSec);
    void rebuild();         // the frame changed size: make it again
    void dragCorner();      // resize while the corner is held
    size_t rows() const;    // message rows that fit in the frame

    std::function<void(const ChatSend&)> send_;
    std::deque<Line> lines_;
    bool typing_ = false;
    bool enterWasDown_ = true;  // ignore an Enter already held when the world appears
    bool escWasDown_ = false;
    double lastDrawn_ = -1;
    size_t drawnCount_ = 0;
    float opacity_ = 85.0f;  // percent
    float appliedOpacity_ = -1;
    int boxW_ = 775, boxH_ = 210;
    int boxLeft_ = -1, boxTop_ = -1;  // pixels; set when the frame is first made
    bool dragging_ = false;
    bool mouseWasDown_ = false;
    bool resized_ = false;
};

}  // namespace wok
