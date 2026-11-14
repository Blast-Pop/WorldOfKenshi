#pragma once
// Avatars of remote players: one real Kenshi character per remote player, spawned in its own
// squad when the player is first seen, moved to the interpolated network position every frame.

#include "spawner.hpp"

#include <core/interp.hpp>
#include <wok/protocol.hpp>

#include <unordered_map>

namespace wok {

class AvatarManager {
public:
    bool init(uintptr_t gameWorld);
    void onMessage(const Message& msg, double nowSec);
    void update(double nowSec);  // game thread, every frame
    void clear();                // disconnected: forget every remote player
    bool inGame() const { return spawner_.inGame(); }
    bool restorePlayer(uintptr_t playerCharacter, const CharacterData& data) const {
        return spawner_.restorePlayerCharacter(playerCharacter, data);
    }

private:
    struct Avatar {
        Interpolator interp;
        uintptr_t character = 0;
        double nextSpawnTry = 0;
        std::string name;    // the remote player's character name
        bool named = false;
        bool walking = false;
        float lastPos[3] = {0, 0, 0};
        double nextTrace = 0;
        double nextOrder = 0;  // next walk order allowed (seconds)
    };
    Spawner spawner_;
    std::unordered_map<PlayerId, Avatar> avatars_;
    uintptr_t faction_ = 0;
    bool wasInGame_ = false;
};

}  // namespace wok
