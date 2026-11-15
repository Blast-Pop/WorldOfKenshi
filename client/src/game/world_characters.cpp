#include "world_characters.hpp"

#include "game_memory.hpp"

#include <unordered_set>

namespace wok {

namespace {
constexpr uintptr_t kGameWorld = 0x2134110;
constexpr uintptr_t kBucketCount = 0x780;  // size_t
constexpr uintptr_t kBuckets = 0x788;      // node** (bucket_count + 1 entries)
}  // namespace

std::vector<uintptr_t> activeCharacters(size_t limit) {
    std::vector<uintptr_t> out;
    const uintptr_t gw = mem::moduleBase() + kGameWorld;
    uint64_t count = 0;
    uintptr_t buckets = 0;
    if (!mem::readBytes(gw + kBucketCount, &count, sizeof(count)) || count == 0 || count > 1u << 20 ||
        !mem::readPtr(gw + kBuckets, buckets) || !buckets)
        return out;
    std::unordered_set<uintptr_t> seen, nodes;
    // Each bucket points into one singly linked list of nodes {next, ..., Character*}; walk them all.
    for (uint64_t b = 0; b <= count && out.size() < limit; ++b) {
        uintptr_t node = 0;
        if (!mem::readPtr(buckets + b * 8, node)) continue;
        for (int steps = 0; node > 0x10000 && steps < 4096 && out.size() < limit; ++steps) {
            if (!nodes.insert(node).second) break;  // already walked from another bucket
            for (uintptr_t k = 8; k <= 0x18; k += 8) {
                uintptr_t v = 0;
                if (mem::readPtr(node + k, v) && v > 0x10000 && mem::isA(v, ".?AVCharacterHuman@@") && seen.insert(v).second)
                    out.push_back(v);
            }
            uintptr_t next = 0;
            if (!mem::readPtr(node, next)) break;
            node = next;
        }
    }
    return out;
}

}  // namespace wok
