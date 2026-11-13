#pragma once
#include <windows.h>

namespace wok {

// True once per key press (edge-triggered), only while Kenshi's window has focus.
// Call every frame for each key you watch.
bool keyPressed(int vk);

}  // namespace wok
