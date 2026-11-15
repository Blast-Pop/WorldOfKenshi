#include "shared_npcs.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "identity.hpp"
#include "orders.hpp"
#include "stats.hpp"
#include "world_characters.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wok {

namespace {
constexpr uintptr_t kCharPosition = 0x48;     // Vector3
constexpr uintptr_t kCharOrientation = 0xb0;  // Quaternion (w, x, y, z), offsets.hpp
constexpr double kReportInterval = 0.25;      // owner: NPC snapshots, 4 a second
constexpr double kAnnounceAllInterval = 10.0; // owner: every NPC's info again, for newcomers
constexpr double kPuppetTimeout = 5.0;        // viewer: a puppet not reported for this long goes away
constexpr double kPuppetOrderInterval = 1.0;  // viewer: a puppet gets a new move order at most this often
constexpr float kPuppetOrderMove = 2.0f;      // ...and only when its target moved this far
constexpr float kTeleportGap = 40.0f;
constexpr float kRestoreGap = 5.0f;           // ...and is put back where it was if further than this
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
    records_.clear();
}

PlayerId SharedNpcs::ownerOf(float x, float z) const {
    auto it = owners_.find({cellOf(x), cellOf(z)});
    return it == owners_.end() ? 0 : it->second;
}

void SharedNpcs::onMessage(const Message& msg, double nowSec) {
    if (auto* co = std::get_if<CellOwners>(&msg)) {
        owners_.clear();
        for (const CellOwner& c : co->cells) owners_[{c.cx, c.cz}] = c.owner;
        log("shared: %zu cell owner(s) received", owners_.size());
        announced_.clear();  // a new map: tell the (possibly new) neighbours everything again
    } else if (auto* recs = std::get_if<NpcRecords>(&msg)) {
        for (const NpcRecord& r : recs->records) {
            std::erase_if(records_, [&](const NpcRecord& old) { return old.id == r.id; });
            records_.push_back(r);
        }
        log("shared: the server remembers %zu NPC(s) of our new cells (%zu to match)", recs->records.size(),
            records_.size());
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
        if (added) {
            ++nextKey_;
            if (applyRecord(ch, it->second, pos, send)) {  // dead in the world's memory: gone here too
                keys_.erase(it);
                continue;
            }
        }
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
    if (restored_ || removedDead_) {
        log("shared: %d NPC(s) continued from the server's memory, %d dead one(s) removed", restored_, removedDead_);
        restored_ = removedDead_ = 0;
    }
}

bool SharedNpcs::applyRecord(uintptr_t character, uint32_t key, float pos[3], const Send& send) {
    if (records_.empty()) return false;
    const std::string tpl = templateIdOf(character), name = nameOf(character);
    size_t best = records_.size();
    // The nearest record of the same kind, however far: a new game places a town's people anywhere
    // in it, and the records are those of our own cells only.
    float bestD2 = 3.0e38f;
    for (size_t i = 0; i < records_.size(); ++i) {
        const NpcRecord& r = records_[i];
        if (r.templateId != tpl || r.name != name) continue;
        const float dx = r.x - pos[0], dz = r.z - pos[2];
        if (dx * dx + dz * dz < bestD2) {
            bestD2 = dx * dx + dz * dz;
            best = i;
        }
    }
    if (best == records_.size()) return false;
    const NpcRecord r = records_[best];
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(best));
    if (r.dead) {
        if (!spawner_.destroy(character)) return false;
        ++removedDead_;
        return true;
    }
    send(NpcBind{{{key, r.id}}}, true);
    if (bestD2 > kRestoreGap * kRestoreGap) {
        float to[3] = {r.x, r.y, r.z};
        spawner_.setPosition(character, to);
        std::memcpy(pos, to, sizeof(to));
    }
    ++restored_;
    return false;
}

void SharedNpcs::removeOthersNpcs(PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar) {
    me_ = me;
    // Our own copies of NPCs in a cell someone else simulates: the owner's puppets replace them.
    int removed = 0;
    for (uintptr_t ch : activeCharacters()) {
        if (ch == self || isAvatar(ch) || isPuppet(ch)) continue;
        float pos[3];
        if (!mem::readBytes(ch + kCharPosition, pos, sizeof(pos)) || !std::isfinite(pos[0])) continue;
        const PlayerId owner = ownerOf(pos[0], pos[2]);
        if (owner == 0 || owner == me) continue;
        if (spawner_.destroy(ch)) {
            ++removed;
            ++removedLocal_;
        }
    }
    static double lastTrace = 0;
    const double t = GetTickCount64() / 1000.0;
    if (t - lastTrace > 10.0) {  // diagnostic while the shared world is new
        lastTrace = t;
        float me[3] = {};
        mem::readBytes(self + kCharPosition, me, sizeof(me));
        size_t total = 0, inOthers = 0;
        for (uintptr_t ch : activeCharacters()) {
            ++total;
            float pos[3];
            if (mem::readBytes(ch + kCharPosition, pos, sizeof(pos))) {
                const PlayerId o = ownerOf(pos[0], pos[2]);
                if (o && o != me_) ++inOthers;
            }
        }
        int maxQueue = 0, alive = 0;
        for (auto& [id, p] : puppets_)
            if (p.character && spawner_.isAlive(p.character)) {
                ++alive;
                maxQueue = std::max(maxQueue, orderCount(p.character));
            }
        log("shared: my cell (%d, %d) owner %u (me %u); %zu active characters, %zu in others' cells; puppets %d "
            "(spawned %d, destroyed %d), orders %d, longest order list %d, local NPCs removed %d",
            cellOf(me[0]), cellOf(me[2]), ownerOf(me[0], me[2]), me_, total, inOthers, alive, spawned_, destroyed_,
            orders_, maxQueue, removedLocal_);
        spawned_ = destroyed_ = orders_ = removedLocal_ = 0;
    }
    if (removed) log("shared: removed %d local NPC(s) simulated by another player", removed);
}

void SharedNpcs::drivePuppets(double nowSec, PlayerId me) {
    for (auto it = puppets_.begin(); it != puppets_.end();) {
        Puppet& p = it->second;
        const PlayerId owner = static_cast<PlayerId>(it->first >> 32);
        const bool gone = p.lastSeen == 0 ? false : nowSec - p.lastSeen > kPuppetTimeout;
        if (gone || owner == me || ownerOf(p.pos[0], p.pos[2]) == me) {  // not reported, or ours now
            if (p.character && spawner_.isAlive(p.character) && spawner_.destroy(p.character)) ++destroyed_;
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
                ++spawned_;
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
            if (mx * mx + mz * mz > kPuppetOrderMove * kPuppetOrderMove) {
                orderMoveTo(p.character, p.pos);
                ++orders_;
                std::memcpy(p.sentPos, p.pos, sizeof(p.pos));
                p.nextOrder = nowSec + kPuppetOrderInterval;
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
