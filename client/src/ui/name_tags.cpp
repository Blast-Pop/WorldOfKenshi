#include "name_tags.hpp"

#include "../game/direct_control.hpp"
#include "../log.hpp"
#include "mygui_abi.hpp"
#include "../game/game_memory.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace wok {

namespace {
constexpr float kHeadHeight = 2.4f;  // label anchor above the feet
constexpr int kTagW = 260, kTagH = 32;  // Kenshi_CharacterNamePanel.layout
constexpr int kLiftPx = 45;  // above the head
constexpr size_t kMaxTags = 32;
std::string tagName(size_t i) { return "WokNameTag" + std::to_string(i); }
std::string bubbleName(size_t tag, size_t row) { return "WokSaid" + std::to_string(tag) + "_" + std::to_string(row); }
constexpr const char* kSaidSkin = "Kenshi_TextboxStandardText";
constexpr int kSaidW = 320, kSaidRowH = 20;
constexpr size_t kSaidRows = 4;  // at most, the end of a longer message is cut

using MatrixFn = const float* (*)(const void* self);
using RefVecFn = const float* (*)(const void* self);          // const Vector3& getter
using ValVecFn = float* (*)(const void* self, float* out);    // Vector3 returned by value
using NodeOfFn = void* (*)(const void* movable);
using AabbFn = float* (*)(const void* movable, float* out);  // Aabb {center, halfSize} by value
struct Calls {
    AabbFn worldAabb = nullptr;
    NodeOfFn parentNode = nullptr;
    ValVecFn nodePosition = nullptr;
    MatrixFn projection = nullptr;
    RefVecFn position = nullptr;
    ValVecFn right = nullptr, up = nullptr, direction = nullptr;
};
const Calls& calls() {
    static Calls c = [] {
        Calls r;
        if (HMODULE m = GetModuleHandleA("OgreMain_x64.dll")) {
            r.worldAabb = reinterpret_cast<AabbFn>(GetProcAddress(m, "?getWorldAabb@MovableObject@Ogre@@QEBA?AUAabb@2@XZ"));
            r.parentNode = reinterpret_cast<NodeOfFn>(
                GetProcAddress(m, "?getParentSceneNode@MovableObject@Ogre@@QEBAPEAVSceneNode@2@XZ"));
            r.nodePosition =
                reinterpret_cast<ValVecFn>(GetProcAddress(m, "?_getDerivedPosition@Node@Ogre@@QEBA?AVVector3@2@XZ"));
            r.projection =
                reinterpret_cast<MatrixFn>(GetProcAddress(m, "?getProjectionMatrix@Frustum@Ogre@@UEBAAEBVMatrix4@2@XZ"));
            r.position = reinterpret_cast<RefVecFn>(GetProcAddress(m, "?getDerivedPosition@Camera@Ogre@@QEBAAEBVVector3@2@XZ"));
            r.right = reinterpret_cast<ValVecFn>(GetProcAddress(m, "?getDerivedRight@Camera@Ogre@@QEBA?AVVector3@2@XZ"));
            r.up = reinterpret_cast<ValVecFn>(GetProcAddress(m, "?getDerivedUp@Camera@Ogre@@QEBA?AVVector3@2@XZ"));
            r.direction = reinterpret_cast<ValVecFn>(GetProcAddress(m, "?getDerivedDirection@Camera@Ogre@@QEBA?AVVector3@2@XZ"));
        }
        return r;
    }();
    return c;
}

// The camera's world position and axes, and its projection. Kenshi renders relative to the camera,
// so Ogre's view matrix is not in world coordinates: the view transform is rebuilt from the axes.
struct CameraFrame {
    float pos[3], right[3], up[3], dir[3], proj[16];
};
bool readCamera(CameraFrame& f) {
    const Calls& c = calls();
    const uintptr_t cam = findGameCamera();
    if (!c.projection || !c.position || !c.right || !c.up || !c.direction || !cam) return false;
    __try {
        const void* self = reinterpret_cast<void*>(cam);
        const float* p = c.position(self);
        for (int i = 0; i < 3; ++i) f.pos[i] = p[i];
        c.right(self, f.right);
        c.up(self, f.up);
        c.direction(self, f.dir);
        const float* m = c.projection(self);
        for (int i = 0; i < 16; ++i) f.proj[i] = m[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Where a character is drawn (render space, same as the camera's): the scene node of its body, an
// Ogre::Entity held by its appearance (character + 0x448). Game positions (character + 0x48) are
// in another space: Kenshi keeps the rendered world near the camera.
bool renderPositionRaw(uintptr_t entity, float out[3]) {
    const Calls& c = calls();
    __try {
        if (c.worldAabb) {  // the top of the body's bounding box: right above the head, any size
            float box[6] = {};
            c.worldAabb(reinterpret_cast<void*>(entity), box);
            if (box[4] > 0.0f) {
                out[0] = box[0];
                out[1] = box[1] + box[4];
                out[2] = box[2];
                return true;
            }
        }
        void* node = c.parentNode(reinterpret_cast<void*>(entity));
        if (!node) return false;
        c.nodePosition(node, out);
        out[1] += kHeadHeight;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool renderPosition(uintptr_t character, float out[3]) {
    const Calls& c = calls();
    uintptr_t appearance = 0;
    if (!c.parentNode || !c.nodePosition || !mem::readPtr(character + 0x448, appearance) || !appearance) return false;
    static uintptr_t entityOffset = 0;  // found once
    uintptr_t entity = 0;
    if (!entityOffset || !mem::readPtr(appearance + entityOffset, entity) || !mem::isA(entity, ".?AVEntity@Ogre@@")) {
        entity = 0;
        for (uintptr_t off = 8; off < 0x400 && !entity; off += 8) {
            uintptr_t p = 0;
            if (mem::readPtr(appearance + off, p) && p > 0x10000 && mem::isA(p, ".?AVEntity@Ogre@@")) {
                entity = p;
                entityOffset = off;
                log("tags: body entity at appearance + 0x%llx", static_cast<unsigned long long>(off));
            }
        }
    }
    return entity && renderPositionRaw(entity, out);
}

// Ogre matrices are row-major: out = m * (x, y, z, w).
void mul(const float m[16], const float in[4], float out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = m[r * 4] * in[0] + m[r * 4 + 1] * in[1] + m[r * 4 + 2] * in[2] + m[r * 4 + 3] * in[3];
}

void viewSize(int& w, int& h) {
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    HWND found = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
            *reinterpret_cast<HWND*>(lp) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    RECT r;
    if (found && GetClientRect(found, &r) && r.right > 0 && r.bottom > 0) {
        w = r.right;
        h = r.bottom;
    }
}
}  // namespace

void updateNameTags(const std::vector<NameTag>& tags, bool visible) {
    if (!mygui::available()) return;
    static size_t shown = 0;
    CameraFrame cam{};
    const bool ok = visible && !tags.empty() && readCamera(cam);
    static double lastLog = 0;  // diagnostic, every 10 s while there are tags
    const double nowSec = GetTickCount64() / 1000.0;
    const bool trace = !tags.empty() && nowSec - lastLog > 10.0;
    if (trace) lastLog = nowSec;
    static int vw = 0, vh = 0;
    static unsigned frame = 0;
    if (ok && (frame++ % 120 == 0 || vw == 0)) viewSize(vw, vh);  // window size, re-read now and then
    size_t used = 0;
    for (size_t i = 0; ok && i < tags.size() && used < kMaxTags; ++i) {
        float feet[3];
        if (!renderPosition(tags[i].character, feet)) continue;
        const float world[4] = {feet[0], feet[1], feet[2], 1.0f};  // already above the head
        const float d[3] = {world[0] - cam.pos[0], world[1] - cam.pos[1], world[2] - cam.pos[2]};
        auto dot = [&](const float* a) { return a[0] * d[0] + a[1] * d[1] + a[2] * d[2]; };
        // Camera space: x right, y up, looking down -z.
        const float eye[4] = {dot(cam.right), dot(cam.up), -dot(cam.dir), 1.0f};
        float clip[4];
        mul(cam.proj, eye, clip);
        if (trace)
            log("tags: '%s' clip (%.2f, %.2f, %.2f, %.2f) view %d x %d", tags[i].name.c_str(), clip[0], clip[1], clip[2],
                clip[3], vw, vh);
        if (clip[3] <= 0.01f) continue;  // behind the camera
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f) continue;  // off screen
        const int sx = static_cast<int>((nx * 0.5f + 0.5f) * vw), sy = static_cast<int>((0.5f - ny * 0.5f) * vh);
        const std::string name = tagName(used);
        mygui::Widget* w = mygui::findWidget(name);
        if (tags[i].name.empty()) {
            mygui::destroy(w);
            w = nullptr;
        } else if (!w) {
            // Kenshi's own name tag look (Kenshi_CharacterNamePanel.layout, NameTagUnselected colour).
            w = mygui::createRoot("TextBox", "Kenshi_TextboxStandardText_Large", {0, 0, 0.1f, 0.03f}, "Back", name);
            if (!w) continue;
            mygui::setProperty(w, "TextAlign", "Center");
            mygui::setProperty(w, "TextColour", "0.7 0.7 0.7");
            mygui::setProperty(w, "TextShadow", "true");
            mygui::setProperty(w, "TextShadowColour", "0.28 0.28 0.28");
            mygui::setProperty(w, "NeedMouse", "false");
        }
        if (w) {
            mygui::setProperty(w, "Caption", "[" + tags[i].name + "]");
            mygui::setCoord(w, sx - kTagW / 2, sy - kTagH / 2 - kLiftPx, kTagW, kTagH);
        }
        // What the character just said in local chat, above its name.
        std::vector<std::string> said;
        if (!tags[i].said.empty()) {
            said = mygui::wrapText(kSaidSkin, tags[i].said, kSaidW);
            if (said.size() > kSaidRows) {
                said.resize(kSaidRows);
                said.back() += "...";
            }
        }
        const int saidBottom = sy - kTagH / 2 - kLiftPx - (w ? 2 : -kTagH);
        for (size_t r = 0; r < kSaidRows; ++r) {
            mygui::Widget* b = mygui::findWidget(bubbleName(used, r));
            if (r >= said.size()) {
                mygui::destroy(b);
                continue;
            }
            if (!b) {
                b = mygui::createRoot("TextBox", kSaidSkin, {0, 0, 0.1f, 0.02f}, "Back", bubbleName(used, r));
                if (!b) continue;
                mygui::setProperty(b, "TextAlign", "Center");
                mygui::setProperty(b, "TextColour", "1 1 1");
                mygui::setProperty(b, "TextShadow", "true");
                mygui::setProperty(b, "TextShadowColour", "0 0 0");
                mygui::setProperty(b, "NeedMouse", "false");
            }
            mygui::setProperty(b, "Caption", said[r]);
            const int top = saidBottom - static_cast<int>(said.size() - r) * kSaidRowH;
            mygui::setCoord(b, sx - kSaidW / 2, top, kSaidW, kSaidRowH);
        }
        ++used;
    }
    if (trace) log("tags: %zu tag(s), matrices %s, %zu label(s) shown", tags.size(), ok ? "ok" : "MISSING", used);
    for (size_t i = used; i < shown; ++i) {
        mygui::destroy(mygui::findWidget(tagName(i)));
        for (size_t r = 0; r < kSaidRows; ++r) mygui::destroy(mygui::findWidget(bubbleName(i, r)));
    }
    shown = used;
}

}  // namespace wok
