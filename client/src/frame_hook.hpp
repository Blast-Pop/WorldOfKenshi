#pragma once
#include <functional>

namespace wok {

// Registers a FrameListener on Ogre::Root. `onFrame(dtSeconds)` runs on the game thread at the
// start of every frame; exceptions are caught and logged. Returns false if OGRE is unavailable.
bool installFrameHook(std::function<void(float)> onFrame);

}  // namespace wok
