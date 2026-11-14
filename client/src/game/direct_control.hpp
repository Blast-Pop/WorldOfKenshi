#pragma once
// Direct control (WASD): the selected character walks where the keys point, relative to the
// camera, through Kenshi's own move orders (orders.hpp). Native to World of Kenshi, no other mod.

#include <cstdint>

namespace wok {

class DirectControl {
public:
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    // Every frame; `character` is the selected player character (0 if none), `typing` true while a
    // text field has the keyboard (chat, names).
    void update(double nowSec, uintptr_t character, bool typing);

private:
    bool enabled_ = false;
    bool moving_ = false;
    double nextOrder_ = 0;
};

// Horizontal forward direction of the game camera (x, z), normalised. False if unknown.
bool cameraForward(float& x, float& z);

}  // namespace wok
