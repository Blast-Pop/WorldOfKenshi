#pragma once
// Body sync (spec 7.3): animations and health of the characters each player simulates (its own
// character, the NPCs of its cells), replayed on the other players' copies. A copy that loses
// health locally (someone hit it there) sends the damage to the character's owner, who applies it.
// Animations are Ogre animation states, matched by name on the copy's body and set every frame.

#include <wok/protocol.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wok {

class BodySync {
public:
    using Send = std::function<void(const Message&, bool reliable)>;
    // A copy on our screen: kind, owner, key -> the local character showing it (0 if none).
    using ResolveCopy = std::function<uintptr_t(BodyKind, PlayerId owner, uint32_t key)>;
    // A character we simulate: kind, key -> our character (0 if none).
    using ResolveOwn = std::function<uintptr_t(BodyKind, uint32_t key)>;

    // Owner side, every frame: sends our character's and our NPCs' bodies a few times a second.
    void sendOwn(double nowSec, uintptr_t self, const std::vector<std::pair<uint32_t, uintptr_t>>& npcs,
                 const Send& send);
    void onStates(const BodyStates& m, double nowSec, const ResolveCopy& resolve, const Send& send);
    void onHit(const Hit& m, const ResolveOwn& resolve);
    void update(double nowSec);  // every frame: the copies play the owners' animations
    void clear();

private:
    struct Remote {
        PlayerId owner = 0;
        BodyKind kind = BodyKind::Player;
        uint32_t key = 0;
        std::vector<AnimPlay> anims;
        double animsAt = 0, lastSeen = 0;
        std::vector<StatValue> written;  // the health we last gave the copy
    };
    std::unordered_map<uintptr_t, Remote> remotes_;  // by local character
    double nextSend_ = 0, nextTrace_ = 0;
    int applied_ = 0, missing_ = 0, hitsSent_ = 0, hitsTaken_ = 0, sent_ = 0;
};

// The animations playing on a character's body (Ogre animation states with some weight).
std::vector<AnimPlay> readAnims(uintptr_t character);

}  // namespace wok
