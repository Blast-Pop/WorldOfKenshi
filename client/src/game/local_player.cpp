#include "local_player.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "offsets.hpp"

#include <core/motion.hpp>

#include <cmath>

namespace wok {

bool LocalPlayerReader::init() {
    setSigLogger([](const char* m) { log("%s", m); });
    auto text = mem::moduleText();
    piSlot_ = resolve(text, reinterpret_cast<uintptr_t>(text.data()), offsets::kPlayerInterfaceSlot);
    if (!piSlot_) return false;
    log("PlayerInterface slot at exe+0x%llx", static_cast<unsigned long long>(piSlot_ - mem::moduleBase()));
    return true;
}

std::optional<LocalSnapshot> LocalPlayerReader::read(double nowSec) {
    if (!piSlot_) return std::nullopt;
    uintptr_t pi = 0, selection = 0, ch = 0;
    if (!mem::readPtr(piSlot_, pi) || !pi) return std::nullopt;
    if (mem::readPtr(pi + offsets::kPiSelectedChars, selection) && mem::readPtr(selection, ch) &&
        mem::isA(ch, offsets::kRttiCharacterHuman))
        lastChar_ = ch;
    else if (!lastChar_ || !mem::isA(lastChar_, offsets::kRttiCharacterHuman)) {
        lastChar_ = 0;
        return std::nullopt;
    }

    float pos[3], quat[4];
    if (!mem::readBytes(lastChar_ + offsets::kCharPosition, pos, sizeof(pos)) ||
        !mem::readBytes(lastChar_ + offsets::kCharOrientation, quat, sizeof(quat)))
        return std::nullopt;
    for (float f : pos)
        if (!std::isfinite(f)) return std::nullopt;

    float speed = 0;
    if (lastTime_ >= 0 && nowSec > lastTime_)
        speed = std::hypot(pos[0] - lastX_, pos[2] - lastZ_) / static_cast<float>(nowSec - lastTime_);
    lastX_ = pos[0];
    lastZ_ = pos[2];
    lastTime_ = nowSec;

    return LocalSnapshot{pos[0], pos[1], pos[2], yawFromQuat(quat[0], quat[1], quat[2], quat[3]),
                         animFromSpeed(speed)};
}

}  // namespace wok
