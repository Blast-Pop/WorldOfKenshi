#include "direct_control.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "orders.hpp"

#include <windows.h>

#include <cmath>

namespace wok {

namespace {

constexpr uintptr_t kCharPosition = 0x48;    // Character: world position (Vector3)
constexpr float kLookAhead = 15.0f;          // each order targets a point this far ahead
constexpr double kOrderInterval = 0.2;       // seconds between orders while a key is held

// Ogre 2.0 (OgreMain_x64.dll exports): the current scene manager, its cameras, a camera's direction.
using GetRootFn = void* (*)();
using CurrentSceneFn = void* (*)(const void* root);
using GetCamerasFn = const void* (*)(const void* sceneManager);     // const vector<Camera*>&
using DirectionFn = float* (*)(const void* camera, float* out);     // Vector3 returned by value

struct OgreCalls {
    GetRootFn root = nullptr;
    CurrentSceneFn scene = nullptr;
    GetCamerasFn cameras = nullptr;
    DirectionFn direction = nullptr;
    bool ok = false;
};

const OgreCalls& ogre() {
    static OgreCalls c = [] {
        OgreCalls r;
        HMODULE m = GetModuleHandleA("OgreMain_x64.dll");
        if (!m) return r;
        r.root = reinterpret_cast<GetRootFn>(GetProcAddress(m, "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ"));
        r.scene = reinterpret_cast<CurrentSceneFn>(
            GetProcAddress(m, "?_getCurrentSceneManager@Root@Ogre@@QEBAPEAVSceneManager@2@XZ"));
        r.cameras = reinterpret_cast<GetCamerasFn>(GetProcAddress(
            m, "?getCameras@SceneManager@Ogre@@QEBAAEBV?$vector@PEAVCamera@Ogre@@V?$STLAllocator@PEAVCamera@Ogre@@"
               "V?$CategorisedAllocPolicy@$0A@@2@@2@@std@@XZ"));
        r.direction = reinterpret_cast<DirectionFn>(
            GetProcAddress(m, "?getDerivedDirection@Camera@Ogre@@QEBA?AVVector3@2@XZ"));
        r.ok = r.direction != nullptr;
        if (!r.ok) log("control: Ogre camera exports not found");
        return r;
    }();
    return c;
}

// The game camera (an Ogre::Camera, known by its exported vtable). Kenshi keeps it at +0x58 of its
// camera object (made where "mainCamera" is created, RVA 0x818970); that object is found once from
// GameWorld or PlayerInterface, then re-read each time (cheap, and safe if it changes).
uintptr_t findGameCamera() {
    static uintptr_t cameraVt = [] {
        HMODULE m = GetModuleHandleA("OgreMain_x64.dll");
        return m ? reinterpret_cast<uintptr_t>(GetProcAddress(m, "??_7Camera@Ogre@@6BMovableObject@1@@")) : 0;
    }();
    if (!cameraVt) return 0;
    auto isCamera = [&](uintptr_t p) {
        uintptr_t v = 0;
        return p && mem::readPtr(p, v) && v == cameraVt;
    };
    const uintptr_t gw = mem::moduleBase() + 0x2134110;
    uintptr_t pi = 0;
    mem::readPtr(gw + 0x580, pi);
    static uintptr_t holderSlot = 0;  // address of the pointer to Kenshi's camera object
    uintptr_t holder = 0, cam = 0;
    if (holderSlot && mem::readPtr(holderSlot, holder) && mem::readPtr(holder + 0x58, cam) && isCamera(cam)) return cam;
    for (uintptr_t root : {gw, pi}) {
        if (!root) continue;
        for (uintptr_t off = 0; off < 0x1000; off += 8)
            if (mem::readPtr(root + off, holder) && holder && mem::readPtr(holder + 0x58, cam) && isCamera(cam)) {
                holderSlot = root + off;
                log("control: game camera found (%s + 0x%llx, camera %p)", root == gw ? "GameWorld" : "PlayerInterface",
                    static_cast<unsigned long long>(off), reinterpret_cast<void*>(cam));
                return cam;
            }
    }
    static bool logged = false;
    if (!logged) log("control: game camera not found");
    logged = true;
    return 0;
}

bool readCameraDirection(float dir[3]) {
    const OgreCalls& c = ogre();
    uintptr_t cam = findGameCamera();
    if (!c.direction || !cam) return false;
    __try {
        c.direction(reinterpret_cast<void*>(cam), dir);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool gameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace

bool cameraForward(float& x, float& z) {
    float d[3];
    if (!readCameraDirection(d)) return false;
    float len = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    if (!(len > 1e-4f)) return false;
    x = d[0] / len;
    z = d[2] / len;
    return true;
}

void DirectControl::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    moving_ = false;
    log("control: direct control %s", on ? "on" : "off");
}

void DirectControl::update(double nowSec, uintptr_t character, bool typing) {
    if (!enabled_ || !character) return;
    float ahead = 0, side = 0;
    if (!typing && gameHasFocus()) {
        ahead = static_cast<float>(keyDown('W')) - static_cast<float>(keyDown('S'));
        side = static_cast<float>(keyDown('D')) - static_cast<float>(keyDown('A'));
    }
    float pos[3];
    if (!mem::readBytes(character + kCharPosition, pos, sizeof(pos))) return;
    if (ahead == 0 && side == 0) {
        if (moving_) {  // keys released: stop where we are
            moving_ = false;
            orderMoveTo(character, pos);
        }
        return;
    }
    if (moving_ && nowSec < nextOrder_) return;
    float fx = 0, fz = 0;
    if (!cameraForward(fx, fz)) return;
    // Right of the camera on the ground plane (y up): (-fz, fx) turned the other way is (fz, -fx)...
    // Ogre is right-handed with y up: right = forward x up = (-fz, 0, fx).
    float dx = fx * ahead - fz * side;
    float dz = fz * ahead + fx * side;
    float len = std::sqrt(dx * dx + dz * dz);
    if (!(len > 1e-4f)) return;
    float target[3] = {pos[0] + dx / len * kLookAhead, pos[1], pos[2] + dz / len * kLookAhead};
    bool ok = orderMoveTo(character, target);
    static int logs = 0;
    if (logs < 6) {
        ++logs;
        log("control: at (%.1f, %.1f) camera (%.2f, %.2f) keys %+.0f/%+.0f -> order (%.1f, %.1f) %s", pos[0], pos[2], fx, fz,
            ahead, side, target[0], target[2], ok ? "given" : "FAILED");
    }
    moving_ = true;
    nextOrder_ = nowSec + kOrderInterval;
}

}  // namespace wok
