#pragma once
// Other players' names, always shown above their avatars so players recognise each other: our own
// MyGUI labels placed every frame by projecting the avatar's head with the game camera.

#include <string>
#include <vector>

namespace wok {

struct NameTag {
    std::string name;
    float pos[3];  // the avatar's feet (world)
};

// Every frame; an empty list (or `visible` false) removes the labels.
void updateNameTags(const std::vector<NameTag>& tags, bool visible);

}  // namespace wok
