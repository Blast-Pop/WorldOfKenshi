#include "frame_hook.hpp"

#include "log.hpp"
#include "ogre_abi.hpp"

#include <windows.h>

namespace wok {

namespace {

std::function<void(float)> g_onFrame;
bool g_failed = false;

class WokFrameListener final : public ogre_abi::FrameListener {
public:
    bool frameStarted(const ogre_abi::FrameEvent& evt) override {
        if (g_onFrame && !g_failed) {
            try {
                g_onFrame(evt.timeSinceLastFrame);
            } catch (const std::exception& e) {
                log("frame callback exception: %s (callback disabled)", e.what());
                g_failed = true;
            } catch (...) {
                log("frame callback unknown exception (callback disabled)");
                g_failed = true;
            }
        }
        return true;  // returning false would stop Kenshi's render loop
    }
};

WokFrameListener g_listener;

}  // namespace

bool installFrameHook(std::function<void(float)> onFrame) {
    HMODULE ogre = GetModuleHandleA("OgreMain_x64.dll");
    if (!ogre) {
        log("frame hook: OgreMain_x64.dll not loaded");
        return false;
    }
    using GetRootFn = void* (*)();
    using AddListenerFn = void (*)(void* root, ogre_abi::FrameListener* listener);
    auto getRoot = reinterpret_cast<GetRootFn>(GetProcAddress(ogre, "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ"));
    auto addListener =
        reinterpret_cast<AddListenerFn>(GetProcAddress(ogre, "?addFrameListener@Root@Ogre@@QEAAXPEAVFrameListener@2@@Z"));
    if (!getRoot || !addListener) {
        log("frame hook: Ogre::Root exports not found");
        return false;
    }
    void* root = getRoot();
    if (!root) {
        log("frame hook: Ogre::Root not created yet");
        return false;
    }
    g_onFrame = std::move(onFrame);
    addListener(root, &g_listener);
    log("frame hook installed (Root=%p)", root);
    return true;
}

}  // namespace wok
