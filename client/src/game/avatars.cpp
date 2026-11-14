#include "avatars.hpp"

#include "../log.hpp"

#include <cmath>
#include <cstring>

namespace wok {

namespace {
// Temporary: until our own FCS data mod adds a neutral "World of Kenshi players" faction.
constexpr const char* kAvatarFaction = "Drifters";
constexpr double kSpawnRetrySec = 2.0;
}  // namespace

bool AvatarManager::init(uintptr_t gameWorld) { return spawner_.init(gameWorld); }

void AvatarManager::onMessage(const Message& msg, double nowSec) {
    if (auto* s = std::get_if<PlayerState>(&msg)) {
        avatars_[s->playerId].interp.push(nowSec, *s);
    } else if (auto* left = std::get_if<PlayerLeft>(&msg)) {
        auto it = avatars_.find(left->playerId);
        if (it != avatars_.end()) {
            bool removed = it->second.character && spawner_.destroy(it->second.character);
            log("avatar: player %u left, character %p %s", left->playerId, reinterpret_cast<void*>(it->second.character),
                removed ? "removed" : "not removed");
            avatars_.erase(it);
        }
    }
}

void AvatarManager::clear() { avatars_.clear(); }

void AvatarManager::update(double nowSec) {
    bool inGame = spawner_.inGame();
    if (!inGame) {
        if (wasInGame_) {  // world unloaded: its characters are gone
            for (auto& [id, a] : avatars_) a.character = 0;
            faction_ = 0;
        }
        wasInGame_ = false;
        return;
    }
    wasInGame_ = true;
    if (!faction_) {
        faction_ = spawner_.findFaction(kAvatarFaction);
        if (!faction_) return;
    }

    for (auto& [id, a] : avatars_) {
        auto pose = a.interp.sample(nowSec);
        if (!pose) continue;
        const float pos[3] = {pose->x, pose->y, pose->z};
        if (a.character && !spawner_.isAlive(a.character)) a.character = 0;
        if (!a.character) {
            if (nowSec < a.nextSpawnTry) continue;
            a.nextSpawnTry = nowSec + kSpawnRetrySec;
            a.character = spawner_.spawnCharacter(faction_, pos);
            log("avatar: player %u -> character %p at (%.1f, %.1f, %.1f)", id, reinterpret_cast<void*>(a.character),
                pos[0], pos[1], pos[2]);
            continue;
        }
        // Follow the network position by teleport (walking with Kenshi's own move orders is not
        // working yet: docs/re/orders.md).
        float tx = pos[0] - a.lastPos[0], tz = pos[2] - a.lastPos[2];
        if (tx * tx + tz * tz > 0.25f) {
            spawner_.setPosition(a.character, pos);
            std::memcpy(a.lastPos, pos, sizeof(pos));
        }
    }
}

}  // namespace wok
