#pragma once
// Minimal declarations binary-compatible with Kenshi's OGRE 2.0 build (MSVC 2010).
// Only POD and virtual interfaces cross the boundary - never STL types. See docs/re/ogre.md.

namespace ogre_abi {

struct FrameEvent {
    float timeSinceLastEvent;
    float timeSinceLastFrame;
};

// Vtable order verified against ??_7FrameListener@Ogre@@6B@ in OgreMain_x64.dll.
class FrameListener {
public:
    virtual bool frameStarted(const FrameEvent&) { return true; }          // slot 0
    virtual bool frameRenderingQueued(const FrameEvent&) { return true; }  // slot 1
    virtual void kenshiReserved() {}                                       // slot 2 (Kenshi-specific)
    virtual bool frameEnded(const FrameEvent&) { return true; }            // slot 3
    virtual ~FrameListener() = default;                                    // slot 4
};

}  // namespace ogre_abi
