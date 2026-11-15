#include "avatars.hpp"

#include "game_memory.hpp"
#include "orders.hpp"
#include "avatar_look.hpp"

#include <core/appearance.hpp>

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
    } else if (auto* info = std::get_if<PlayerInfo>(&msg)) {
        auto& a = avatars_[info->playerId];
        a.name = info->name;
        a.named = false;
        if (a.look != info->appearance) {
            a.look = info->appearance;
            a.lookApplied = false;
        }
        if (a.templateId != info->templateId) {
            a.templateId = info->templateId;
            // Spawned before we knew their template: make the body again from it.
            if (a.character && !a.templateUsed && !a.templateId.empty()) {
                spawner_.destroy(a.character);
                a.character = 0;
                a.nextSpawnTry = 0;
            }
        }
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
            uintptr_t tpl = 0;
            if (!a.templateId.empty()) {
                static int characterType = -1;  // GameData type of character templates, found once
                if (characterType >= 0) tpl = spawner_.findGameData(a.templateId, characterType);
                for (int t = 0; !tpl && characterType < 0 && t < 64; ++t)
                    if ((tpl = spawner_.findGameData(a.templateId, t))) {
                        characterType = t;
                        log("avatar: character templates are GameData type %d", t);
                    }
            }
            a.character = spawner_.spawnCharacter(faction_, pos, tpl);
            a.templateUsed = tpl != 0;
            a.lookApplied = false;
            log("avatar: player %u -> character %p at (%.1f, %.1f, %.1f), template '%s' %s", id,
                reinterpret_cast<void*>(a.character), pos[0], pos[1], pos[2], a.templateId.c_str(),
                tpl ? "found" : "not found");
            a.named = false;
            continue;
        }
        if (!a.lookApplied && !a.look.empty()) {  // their exact face and body (exported body file)
            a.lookApplied = true;
            Appearance look = decodeAppearance(a.look);
            if (auto it = look.find("bod2"); it != look.end())
                applyBodyFile(a.character, base64Decode(it->second), "player" + std::to_string(id));
        }
        if (!a.named && !a.name.empty()) {  // the player's name above their avatar
            Spawner::setName(a.character, a.name);
            a.named = true;
        }
        // Follow the network position by walking there with Kenshi's own move orders (pathfinding
        // and animations, docs/re/orders.md); teleport only when far behind (join, lag, fast moves).
        float here[3];
        if (!mem::readBytes(a.character + 0x48, here, sizeof(here))) continue;
        const float dx = pos[0] - here[0], dz = pos[2] - here[2];
        const float gap2 = dx * dx + dz * dz;
        if (nowSec >= a.nextTrace && gap2 > 25.0f) {  // trace only when an avatar lags behind
            a.nextTrace = nowSec + 5.0;
            log("avatar: player %u at (%.1f, %.1f) wants (%.1f, %.1f) gap %.1f %s, order list %d", id, here[0], here[2],
                pos[0], pos[2], std::sqrt(gap2), a.walking ? "walking" : "idle", orderCount(a.character));
        }
        constexpr float kTeleportGap = 40.0f, kArrived = 1.0f;
        if (gap2 > kTeleportGap * kTeleportGap) {
            spawner_.setPosition(a.character, pos);
            a.walking = false;
        } else if (gap2 > kArrived * kArrived) {
            const float mx = pos[0] - a.lastPos[0], mz = pos[2] - a.lastPos[2];
            if ((!a.walking || mx * mx + mz * mz > 0.25f) && nowSec >= a.nextOrder) {
                orderMoveTo(a.character, pos);
                a.walking = true;
                a.nextOrder = nowSec + 0.25;
                std::memcpy(a.lastPos, pos, sizeof(pos));
            }
        } else {
            a.walking = false;
        }
    }
}

std::vector<AvatarManager::Tag> AvatarManager::nameTags() const {
    std::vector<Tag> out;
    for (auto& [id, a] : avatars_) {
        if (!a.character || a.name.empty()) continue;
        out.push_back({a.name, a.character, id});
    }
    return out;
}

}  // namespace wok
