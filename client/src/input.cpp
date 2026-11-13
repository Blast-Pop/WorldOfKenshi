#include "input.hpp"

#include <unordered_map>

namespace wok {

bool keyPressed(int vk) {
    static std::unordered_map<int, bool> wasDown;
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !wasDown[vk];
    wasDown[vk] = down;
    if (!pressed) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace wok

