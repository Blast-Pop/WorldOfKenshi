#include "shared_npcs.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "identity.hpp"
#include "orders.hpp"
#include "stats.hpp"
#include "world_characters.hpp"

#include <cmath>
#include <cstring>

namespace wok {

namespace {
constexpr uintptr_t kCharPosition = 0x48;     // Vector3
constexpr uintptr_t kCharOrientation = 0xb0;  // Quaternion (w, x, y, z), offsets.hpp
constexpr double kReportInterval = 0.25;      // owner: NPC snapshots, 4 a second
constexpr double kAnnounceAllInterval = 10.0; // owner: every NPC's info again, for newcomers
constexpr double kPuppetTimeout = 5.0;        // viewer: a puppet not reported for this long goes away
constexpr float kTeleportGap = 40.0f;
constexpr const char* kPuppetFaction = "Drifters";

uint64_t puppetId(PlayerId owner, uint32_t key) { return (static_cast<uint64_t>(owner) << 32) | key; }

float yawOf(uintptr_t character) {
    float q[4] = {1, 0, 0, 0};
    if (!mem::readBytes(character + kCharOrientation, q, sizeof(q))) return 0;
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    return std::atan2(2.0f * (w * y + x * z), 1.0f - 2.0f * (y * y + x * x)) * 57.2957795f;
}
}  // namespace

bool SharedNpcs::init(uintptr_t gameWorld) {
    ok_ = spawner_.init(gameWorld);
    return ok_;
}

void SharedNpcs::clear() {
    for (auto& [id, p] : puppets_)
        if (p.character && spawner_.isAlive(p.character)) spawner_.destroy(p.character);
    puppets_.clear();
    puppetChars_.clear();
    owners_.clear();
    keys_.clear();
    announced_.clear();
}

PlayerId SharedNpcs::ownerOf(float x, float z) const {
    auto it = owners_.find({cellOf(x), cellOf(z)});
    return it == owners_.end() ? 0 : it->second;
}

void SharedNpcs::onMessage(const Message& msg, double nowSec) {
    if (auto* co = std::get_if<CellOwners>(&msg)) {
        owners_.clear();
        for (const CellOwner& c : co->cells) owners_[{c.cx, c.cz}] = c.owner;
        announced_.clear();  // a new map: tell the (possibly new) neighbours everything again
    } else if (auto* info = std::get_if<NpcInfo>(&msg)) {
        Puppet& p = puppets_[puppetId(info->owner, info->key)];
        p.name = info->name;
        p.templateId = info->templateId;
    } else if (auto* snap = std::get_if<NpcSnapshot>(&msg)) {
        for (const NpcState& s : snap->npcs) {
            Puppet& p = puppets_[puppetId(snap->owner, s.key)];
            p.pos[0] = s.x;
            p.pos[1] = s.y;
            p.pos[2] = s.z;
            p.dead = s.dead;
            p.lastSeen = nowSec;
        }
    }
}

void SharedNpcs::reportOwnNpcs(double nowSec, PlayerId me, uintptr_t self,
                               const std::function<bool(uintptr_t)>& isAvatar, const Send& send) {
    const bool announceAll = nowSec >= nextAnnounceAll_;
    if (announceAll) nextAnnounceAll_ = nowSec + kAnnounceAllInterval;
    NpcSnapshot snap;
    for (uintptr_t ch : activeCharacters()) {
        if (ch == self || isAvatar(ch) || isPuppet(ch)) continue;
        float pos[3];
        if (!mem::readBytes(ch + kCharPosition, pos, sizeof(pos)) || !std::isfinite(pos[0])) continue;
        if (ownerOf(pos[0], pos[2]) != me) continue;
        auto [it, added] = keys_.try_emplace(ch, nextKey_);
        if (added) ++nextKey_;
        const uint32_t key = it->second;
        if (announceAll || !announced_.count(key)) {
            send(NpcInfo{0, key, nameOf(ch), templateIdOf(ch)}, true);
            announced_.insert(key);
        }
        snap.npcs.push_back({key, pos[0], pos[1], pos[2], yawOf(ch), AnimState::Idle, isDead(ch)});
        if (snap.npcs.size() == kMaxNpcsPerSnapshot) {
            send(snap, false);
            snap.npcs.clear();
        }
    }
    if (!snap.npcs.empty()) send(snap, false);
}

void SharedNpcs::removeOthersNpcs(PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar) {
    // Our own copies of NPCs in a cell someone else simulates: the owner's puppets replace them.
    int removed = 0;
    for (uintptr_t ch : activeCharacters()) {
        if (ch == self || isAvatar(ch) || isPuppet(ch)) continue;
        float pos[3];
        if (!mem::readBytes(ch + kCharPosition, pos, sizeof(pos)) || !std::isfinite(pos[0])) continue;
        const PlayerId owner = ownerOf(pos[0], pos[2]);
        if (owner == 0 || owner == me) continue;
        if (spawner_.destroy(ch)) ++removed;
    }
    if (removed) log("shared: removed %d local NPC(s) simulated by another player", removed);
}

void SharedNpcs::drivePuppets(double nowSec, PlayerId me) {
    for (auto it = puppets_.begin(); it != puppets_.end();) {
        Puppet& p = it->second;
        const PlayerId owner = static_cast<PlayerId>(it->first >> 32);
        const bool gone = p.lastSeen == 0 ? false : nowSec - p.lastSeen > kPuppetTimeout;
        if (gone || owner == me || ownerOf(p.pos[0], p.pos[2]) == me) {  // not reported, or ours now
            if (p.character && spawner_.isAlive(p.character)) spawner_.destroy(p.character);
            puppetChars_.erase(p.character);
            it = puppets_.erase(it);
            continue;
        }
        ++it;
        if (p.lastSeen == 0 || p.templateId.empty()) continue;  // no position or no info yet
        if (p.character && !spawner_.isAlive(p.character)) {
            puppetChars_.erase(p.character);
            p.character = 0;
        }
        if (!p.character) {
            if (nowSec < p.nextSpawnTry || p.dead) continue;
            p.nextSpawnTry = nowSec + 3.0;
            if (!faction_) faction_ = spawner_.findFaction(kPuppetFaction);
            uintptr_t tpl = spawner_.findGameData(p.templateId, 1);
            p.character = spawner_.spawnCharacter(faction_, p.pos, tpl);
            if (p.character) {
                puppetChars_.insert(p.character);
                if (!p.name.empty()) Spawner::setName(p.character, p.name);
                std::memcpy(p.sentPos, p.pos, sizeof(p.pos));
            }
            continue;
        }
        float here[3];
        if (!mem::readBytes(p.character + kCharPosition, here, sizeof(here))) continue;
        const float dx = p.pos[0] - here[0], dz = p.pos[2] - here[2];
        const float gap2 = dx * dx + dz * dz;
        if (gap2 > kTeleportGap * kTeleportGap) {
            spawner_.setPosition(p.character, p.pos);
        } else if (gap2 > 1.0f && nowSec >= p.nextOrder) {
            const float mx = p.pos[0] - p.sentPos[0], mz = p.pos[2] - p.sentPos[2];
            if (mx * mx + mz * mz > 0.25f) {
                orderMoveTo(p.character, p.pos);
                std::memcpy(p.sentPos, p.pos, sizeof(p.pos));
                p.nextOrder = nowSec + 0.25;
            }
        }
    }
}

void SharedNpcs::update(double nowSec, PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar,
                        const Send& send) {
    if (!ok_ || !me || owners_.empty() || !spawner_.inGame()) return;
    if (nowSec >= nextReport_) {
        nextReport_ = nowSec + kReportInterval;
        bool ownSomething = false;
        for (auto& [cell, owner] : owners_)
            if (owner == me) ownSomething = true;
        if (ownSomething) reportOwnNpcs(nowSec, me, self, isAvatar, send);
    }
    if (nowSec >= nextRemoval_) {
        nextRemoval_ = nowSec + 1.0;
        removeOthersNpcs(me, self, isAvatar);
    }
    drivePuppets(nowSec, me);
}

}  // namespace wok
