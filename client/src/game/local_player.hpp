#pragma once
#include <wok/protocol.hpp>

#include <optional>

namespace wok {

struct LocalSnapshot {
    float x, y, z, yaw;
    AnimState anim;
};

// Reads the player's selected character. Returns nullopt when not in game, when nothing is
// selected (and no previously selected character is still valid), or if a signature is missing.
class LocalPlayerReader {
public:
    bool init();  // resolves signatures; logs and returns false on failure
    std::optional<LocalSnapshot> read(double nowSec);

private:
    uintptr_t piSlot_ = 0;
    uintptr_t lastChar_ = 0;
    float lastX_ = 0, lastZ_ = 0;
    double lastTime_ = -1;
};

}  // namespace wok
