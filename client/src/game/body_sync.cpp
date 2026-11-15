#include "body_sync.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "stats.hpp"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <string>

namespace wok {

namespace {

constexpr double kSendInterval = 0.3;   // bodies sent ~3 times a second
constexpr float kHitMin = 2.0f;         // a copy losing more than this between two updates was hit
constexpr double kRemoteTimeout = 10.0; // a copy not updated for this long is forgotten
constexpr size_t kMaxListNodes = 32, kMaxMapNodes = 1024;

// Ogre (OgreMain_x64.dll), the functions Kenshi itself imports to animate characters.
struct Ogre {
    void* (*allStates)(const void* entity) = nullptr;                // Entity::getAllAnimationStates
    void* (*enabledIter)(const void* set, void* out) = nullptr;      // AnimationStateSet::getEnabledAnimationStateIterator
    void* (*stateIter)(void* set, void* out) = nullptr;              // AnimationStateSet::getAnimationStateIterator
    const void* (*name)(const void* state) = nullptr;                // AnimationState::getAnimationName
    float (*time)(const void* state) = nullptr;
    float (*weight)(const void* state) = nullptr;
    float (*length)(const void* state) = nullptr;
    void (*setEnabled)(void* state, bool on) = nullptr;
    void (*setTime)(void* state, float t) = nullptr;
    void (*setWeight)(void* state, float w) = nullptr;
    bool ok = false;
};

const Ogre& ogre() {
    static Ogre o = [] {
        Ogre r;
        HMODULE m = GetModuleHandleA("OgreMain_x64.dll");
        if (!m) return r;
        auto get = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(m, n)); };
        r.allStates = reinterpret_cast<decltype(r.allStates)>(get("?getAllAnimationStates@Entity@Ogre@@QEBAPEAVAnimationStateSet@2@XZ"));
        r.enabledIter = reinterpret_cast<decltype(r.enabledIter)>(get(
            "?getEnabledAnimationStateIterator@AnimationStateSet@Ogre@@QEBA?AV?$ConstVectorIterator@V?$list@PEAVAnimationState@Ogre@@V?$STLAllocator@PEAVAnimationState@Ogre@@V?$CategorisedAllocPolicy@$0A@@Ogre@@@Ogre@@@std@@@2@XZ"));
        r.stateIter = reinterpret_cast<decltype(r.stateIter)>(get(
            "?getAnimationStateIterator@AnimationStateSet@Ogre@@QEAA?AV?$MapIterator@V?$map@IPEAVAnimationState@Ogre@@U?$less@I@std@@V?$STLAllocator@U?$pair@$$CBIPEAVAnimationState@Ogre@@@std@@V?$CategorisedAllocPolicy@$0A@@Ogre@@@Ogre@@@std@@@2@XZ"));
        r.name = reinterpret_cast<decltype(r.name)>(get("?getAnimationName@AnimationState@Ogre@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"));
        r.time = reinterpret_cast<decltype(r.time)>(get("?getTimePosition@AnimationState@Ogre@@QEBAMXZ"));
        r.weight = reinterpret_cast<decltype(r.weight)>(get("?getWeight@AnimationState@Ogre@@QEBAMXZ"));
        r.length = reinterpret_cast<decltype(r.length)>(get("?getLength@AnimationState@Ogre@@QEBAMXZ"));
        r.setEnabled = reinterpret_cast<decltype(r.setEnabled)>(get("?setEnabled@AnimationState@Ogre@@QEAAX_N@Z"));
        r.setTime = reinterpret_cast<decltype(r.setTime)>(get("?setTimePosition@AnimationState@Ogre@@QEAAXM@Z"));
        r.setWeight = reinterpret_cast<decltype(r.setWeight)>(get("?setWeight@AnimationState@Ogre@@QEAAXM@Z"));
        r.ok = r.allStates && r.enabledIter && r.stateIter && r.name && r.time && r.weight && r.length &&
               r.setEnabled && r.setTime && r.setWeight;
        log("body: Ogre animation functions %s", r.ok ? "found" : "MISSING (no animation sync)");
        return r;
    }();
    return o;
}

// The character's body entity (its appearance holds the Ogre Entity; the offset is found once).
uintptr_t bodyEntity(uintptr_t character) {
    static uintptr_t offset = 0;
    uintptr_t appearance = 0, entity = 0;
    if (!mem::readPtr(character + 0x448, appearance) || !appearance) return 0;
    if (offset && mem::readPtr(appearance + offset, entity) && mem::isA(entity, ".?AVEntity@Ogre@@")) return entity;
    for (uintptr_t off = 8; off < 0x400; off += 8) {
        uintptr_t p = 0;
        if (mem::readPtr(appearance + off, p) && p > 0x10000 && mem::isA(p, ".?AVEntity@Ogre@@")) {
            offset = off;
            return p;
        }
    }
    return 0;
}

// Raw calls, guarded (a stale pointer must not take the game down). No C++ objects in here.
void* guardedSet(uintptr_t entity) {
    __try {
        return ogre().allStates(reinterpret_cast<void*>(entity));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
// The enabled states of a set (std::list nodes: next, prev, value).
size_t guardedEnabled(void* set, void** out, size_t max) {
    __try {
        uintptr_t it[3] = {};
        ogre().enabledIter(set, it);
        size_t n = 0;
        for (uintptr_t node = it[0]; node && node != it[2] && n < max; node = *reinterpret_cast<uintptr_t*>(node))
            out[n++] = *reinterpret_cast<void**>(node + 16);
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
// Every state of a set (std::map<unsigned, AnimationState*> nodes: left, parent, right, key, value,
// colour, isnil), in order.
size_t guardedAll(void* set, void** out, size_t max) {
    __try {
        uintptr_t it[3] = {};
        ogre().stateIter(set, it);
        auto isNil = [](uintptr_t n) { return *reinterpret_cast<uint8_t*>(n + 41) != 0; };
        auto field = [](uintptr_t n, int i) { return *reinterpret_cast<uintptr_t*>(n + 8 * i); };
        size_t n = 0;
        for (uintptr_t node = it[0]; node && node != it[2] && n < max;) {
            out[n++] = *reinterpret_cast<void**>(node + 32);
            uintptr_t right = field(node, 2);
            if (!isNil(right)) {
                node = right;
                while (!isNil(field(node, 0))) node = field(node, 0);
            } else {
                uintptr_t parent = field(node, 1);
                while (!isNil(parent) && node == field(parent, 2)) {
                    node = parent;
                    parent = field(parent, 1);
                }
                node = parent;
            }
        }
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
bool guardedRead(void* state, const char** text, size_t* size, float* time, float* weight, float* length) {
    __try {
        const auto* s = reinterpret_cast<const uint8_t*>(ogre().name(state));
        const size_t n = *reinterpret_cast<const size_t*>(s + 16), cap = *reinterpret_cast<const size_t*>(s + 24);
        *text = cap >= 16 ? *reinterpret_cast<const char* const*>(s) : reinterpret_cast<const char*>(s);
        *size = n;
        *time = ogre().time(state);
        *weight = ogre().weight(state);
        *length = ogre().length(state);
        return n < 256;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool guardedPlay(void* state, bool on, float time, float weight) {
    __try {
        ogre().setEnabled(state, on);
        if (on) {
            ogre().setWeight(state, weight);
            ogre().setTime(state, time);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string stateName(void* state, float& time, float& weight, float& length) {
    const char* text = nullptr;
    size_t size = 0;
    if (!guardedRead(state, &text, &size, &time, &weight, &length) || !text) return {};
    char copy[256];
    if (!mem::readBytes(reinterpret_cast<uintptr_t>(text), copy, size)) return {};
    return std::string(copy, size);
}

// Name -> state of a body, built by walking its states once (the same body keeps its states).
std::unordered_map<std::string, void*>& statesByName(void* set) {
    static std::unordered_map<void*, std::unordered_map<std::string, void*>> cache;
    if (cache.size() > 256) cache.clear();
    auto& byName = cache[set];
    if (byName.empty()) {
        void* all[kMaxMapNodes];
        const size_t n = guardedAll(set, all, kMaxMapNodes);
        for (size_t i = 0; i < n; ++i) {
            float t = 0, w = 0, l = 0;
            std::string name = stateName(all[i], t, w, l);
            if (!name.empty()) byName[name] = all[i];
        }
    }
    return byName;
}

}  // namespace

std::vector<AnimPlay> readAnims(uintptr_t character) {
    std::vector<AnimPlay> out;
    if (!ogre().ok) return out;
    const uintptr_t entity = bodyEntity(character);
    void* set = entity ? guardedSet(entity) : nullptr;
    if (!set) return out;
    void* states[kMaxListNodes];
    const size_t n = guardedEnabled(set, states, kMaxListNodes);
    for (size_t i = 0; i < n && out.size() < kMaxAnimsPerBody; ++i) {
        float t = 0, w = 0, l = 0;
        std::string name = stateName(states[i], t, w, l);
        if (!name.empty() && w > 0.02f) out.push_back({name, t, w});
    }
    return out;
}

void BodySync::clear() {
    remotes_.clear();
}

void BodySync::sendOwn(double nowSec, uintptr_t self, const std::vector<std::pair<uint32_t, uintptr_t>>& npcs,
                       const Send& send) {
    if (nowSec < nextSend_) return;
    nextSend_ = nowSec + kSendInterval;
    BodyStates m;
    auto add = [&](BodyKind kind, uint32_t key, uintptr_t ch) {
        if (!ch) return;
        m.bodies.push_back({kind, key, readBodyHealth(ch), readAnims(ch)});
        if (m.bodies.size() == 16) {  // several small packets rather than one big one
            send(m, false);
            sent_ += 16;
            m.bodies.clear();
        }
    };
    add(BodyKind::Player, 0, self);
    for (auto& [key, ch] : npcs) add(BodyKind::Npc, key, ch);
    if (!m.bodies.empty()) {
        sent_ += static_cast<int>(m.bodies.size());
        send(m, false);
    }
}

void BodySync::onStates(const BodyStates& m, double nowSec, const ResolveCopy& resolve, const Send& send) {
    for (const BodyState& b : m.bodies) {
        const uintptr_t ch = resolve(b.kind, m.owner, b.key);
        if (!ch) continue;
        Remote& r = remotes_[ch];
        r.owner = m.owner;
        r.kind = b.kind;
        r.key = b.key;
        r.anims = b.anims;
        r.animsAt = nowSec;
        r.lastSeen = nowSec;
        if (b.health.empty()) continue;
        // Health the copy lost here since we last set it: someone hit it on our screen.
        if (!r.written.empty()) {
            std::vector<StatValue> damage;
            for (const StatValue& now : readBodyHealth(ch))
                for (const StatValue& before : r.written)
                    if (now.id == before.id && before.value - now.value > kHitMin)
                        damage.push_back({now.id, before.value - now.value});
            if (!damage.empty()) {
                send(Hit{0, m.owner, b.kind, b.key, damage}, true);
                ++hitsSent_;
            }
        }
        writeBodyHealth(ch, b.health);  // the owner's truth (our hit comes back in its next update)
        r.written = b.health;
    }
}

void BodySync::onHit(const Hit& m, const ResolveOwn& resolve) {
    const uintptr_t ch = resolve(m.kind, m.key);
    if (!ch) return;
    applyBodyDamage(ch, m.damage);
    ++hitsTaken_;
    log("body: player %u hit our %s %u (%zu part(s))", m.from, m.kind == BodyKind::Player ? "character" : "NPC", m.key,
        m.damage.size());
}

void BodySync::update(double nowSec) {
    for (auto it = remotes_.begin(); it != remotes_.end();)
        it = nowSec - it->second.lastSeen > kRemoteTimeout ? remotes_.erase(it) : std::next(it);
    if (ogre().ok)
        for (auto& [ch, r] : remotes_) {
            if (r.anims.empty()) continue;
            const uintptr_t entity = bodyEntity(ch);
            void* set = entity ? guardedSet(entity) : nullptr;
            if (!set) continue;
            auto& byName = statesByName(set);
            // Turn off what the copy plays on its own, then play the owner's animations where they are now.
            void* enabled[kMaxListNodes];
            const size_t n = guardedEnabled(set, enabled, kMaxListNodes);
            for (size_t i = 0; i < n; ++i) {
                bool wanted = false;
                for (const AnimPlay& a : r.anims) {
                    auto s = byName.find(a.name);
                    if (s != byName.end() && s->second == enabled[i]) wanted = true;
                }
                if (!wanted) guardedPlay(enabled[i], false, 0, 0);
            }
            for (const AnimPlay& a : r.anims) {
                auto s = byName.find(a.name);
                if (s == byName.end()) {
                    ++missing_;
                    continue;
                }
                float t0 = 0, w0 = 0, length = 0;
                stateName(s->second, t0, w0, length);
                float t = a.time + static_cast<float>(nowSec - r.animsAt);
                if (length > 0.01f) t = std::fmod(t, length);
                if (guardedPlay(s->second, true, t, a.weight)) ++applied_;
            }
        }
    if (nowSec >= nextTrace_) {
        nextTrace_ = nowSec + 10.0;
        if (!remotes_.empty() || sent_ || hitsSent_ || hitsTaken_)
            log("body: %zu copies, %d bodies sent, %d animation(s) applied, %d unknown, hits sent %d, taken %d",
                remotes_.size(), sent_, applied_, missing_, hitsSent_, hitsTaken_);
        applied_ = missing_ = hitsSent_ = hitsTaken_ = sent_ = 0;
    }
}

}  // namespace wok
