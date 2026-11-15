#pragma once
// Other players' names, always shown above their avatars so players recognise each other: our own
// MyGUI labels placed every frame by projecting the avatar's head with the game camera.

#include <cstdint>
#include <string>
#include <vector>

namespace wok {

struct NameTag {
    std::string name;
    uintptr_t character = 0;  // the avatar (its body's scene node gives the render position)
};

// Every frame; an empty list (or `visible` false) removes the labels.
void updateNameTags(const std::vector<NameTag>& tags, bool visible);

}  // namespace wok
