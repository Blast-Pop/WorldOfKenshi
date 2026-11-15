#pragma once
// Shared NPCs (spec 7.3). The server gives each occupied cell to one player, its owner, whose game
// simulates the NPCs there as usual and reports them; the other players remove their own copies of
// those NPCs and show puppets that follow the owner's (walking with Kenshi's move orders).

#include "spawner.hpp"

#include <wok/protocol.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wok {

class SharedNpcs {
public:
    using Send = std::function<void(const Message&, bool reliable)>;
    bool init(uintptr_t gameWorld);
    void onMessage(const Message& msg, double nowSec);
    // Game thread, every frame. `me`: our player id; `self`: our character; `isAvatar`: true for the
    // characters that show other players.
    void update(double nowSec, PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar,
                const Send& send);
    void clear();  // disconnected
    bool isPuppet(uintptr_t character) const { return puppetChars_.count(character) != 0; }
    // Body sync: the puppet showing an owner's NPC, our NPC with a key, the NPCs we simulate now.
    uintptr_t puppetOf(PlayerId owner, uint32_t key) const;
    uintptr_t npcByKey(uint32_t key) const;
    std::vector<std::pair<uint32_t, uintptr_t>> ownNpcs() const { return reported_; }

private:
    struct Puppet {
        std::string name, templateId;
        uintptr_t character = 0;
        float pos[3] = {};
        float sentPos[3] = {};   // where we last ordered it to walk
        bool dead = false;
        double lastSeen = 0, nextOrder = 0, nextSpawnTry = 0;
    };
    using CellKey = std::pair<int32_t, int32_t>;
    PlayerId ownerOf(float x, float z) const;  // 0 if nobody owns that cell
    void reportOwnNpcs(double nowSec, PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar,
                       const Send& send);
    void removeOthersNpcs(PlayerId me, uintptr_t self, const std::function<bool(uintptr_t)>& isAvatar);
    void drivePuppets(double nowSec, PlayerId me);
    // Owner side: a newly seen NPC of ours takes over what the world remembers of it (position; a
    // dead one is removed). True if it was removed.
    bool applyRecord(uintptr_t character, uint32_t key, float pos[3], const Send& send);

    Spawner spawner_;
    bool ok_ = false;
    uintptr_t faction_ = 0;
    std::map<CellKey, PlayerId> owners_;
    // Owner side: our NPCs' keys, which ones the others already know, timers.
    std::unordered_map<uintptr_t, uint32_t> keys_;
    std::unordered_set<uint32_t> announced_;
    uint32_t nextKey_ = 1;
    double nextReport_ = 0, nextAnnounceAll_ = 0, nextRemoval_ = 0;
    std::vector<NpcRecord> records_;  // the world's memory of our cells, not matched to an NPC yet
    int restored_ = 0, removedDead_ = 0, released_ = 0;
    std::vector<std::pair<uint32_t, uintptr_t>> reported_;  // (key, character) of the last report
    // Diagnostics (the game slowed down over a session with puppets): counted between traces.
    int spawned_ = 0, destroyed_ = 0, orders_ = 0, teleports_ = 0, removedLocal_ = 0;
    // Viewer side: puppets by (owner << 32 | key).
    std::unordered_map<uint64_t, Puppet> puppets_;
    std::unordered_set<uintptr_t> puppetChars_;
    PlayerId me_ = 0;
};

}  // namespace wok
