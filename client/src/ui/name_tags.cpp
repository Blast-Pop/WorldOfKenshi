#include "name_tags.hpp"

#include "../game/direct_control.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <string>

namespace wok {

namespace {
constexpr float kHeadHeight = 2.4f;  // label anchor above the feet
constexpr int kTagW = 220, kTagH = 22;
constexpr size_t kMaxTags = 32;
std::string tagName(size_t i) { return "WokNameTag" + std::to_string(i); }

using MatrixFn = const float* (*)(const void* self);
struct Calls {
    MatrixFn view = nullptr, projection = nullptr;
};
const Calls& calls() {
    static Calls c = [] {
        Calls r;
        if (HMODULE m = GetModuleHandleA("OgreMain_x64.dll")) {
            r.view = reinterpret_cast<MatrixFn>(GetProcAddress(m, "?getViewMatrix@Camera@Ogre@@UEBAAEBVMatrix4@2@XZ"));
            r.projection =
                reinterpret_cast<MatrixFn>(GetProcAddress(m, "?getProjectionMatrix@Frustum@Ogre@@UEBAAEBVMatrix4@2@XZ"));
        }
        return r;
    }();
    return c;
}

bool readMatrices(float view[16], float proj[16]) {
    const Calls& c = calls();
    const uintptr_t cam = findGameCamera();
    if (!c.view || !c.projection || !cam) return false;
    __try {
        const float* v = c.view(reinterpret_cast<void*>(cam));
        const float* p = c.projection(reinterpret_cast<void*>(cam));
        for (int i = 0; i < 16; ++i) {
            view[i] = v[i];
            proj[i] = p[i];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
    float view[16], proj[16];
    const bool ok = visible && !tags.empty() && readMatrices(view, proj);
    static int vw = 0, vh = 0;
    static unsigned frame = 0;
    if (ok && (frame++ % 120 == 0 || vw == 0)) viewSize(vw, vh);  // window size, re-read now and then
    size_t used = 0;
    for (size_t i = 0; ok && i < tags.size() && used < kMaxTags; ++i) {
        const float world[4] = {tags[i].pos[0], tags[i].pos[1] + kHeadHeight, tags[i].pos[2], 1.0f};
        float eye[4], clip[4];
        mul(view, world, eye);
        mul(proj, eye, clip);
        if (clip[3] <= 0.01f) continue;  // behind the camera
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f) continue;  // off screen
        const int sx = static_cast<int>((nx * 0.5f + 0.5f) * vw), sy = static_cast<int>((0.5f - ny * 0.5f) * vh);
        const std::string name = tagName(used);
        mygui::Widget* w = mygui::findWidget(name);
        if (!w) {
            w = mygui::createRoot("TextBox", "Kenshi_TextboxStandardText", {0, 0, 0.1f, 0.02f}, "Popup", name);
            if (!w) continue;
            mygui::setProperty(w, "TextAlign", "Center");
            mygui::setProperty(w, "TextColour", "0.95 0.85 0.45");
            mygui::setProperty(w, "TextShadow", "true");
            mygui::setProperty(w, "NeedMouse", "false");
        }
        mygui::setProperty(w, "Caption", tags[i].name);
        mygui::setCoord(w, sx - kTagW / 2, sy - kTagH / 2, kTagW, kTagH);
        ++used;
    }
    for (size_t i = used; i < shown; ++i) mygui::destroy(mygui::findWidget(tagName(i)));
    shown = used;
}

}  // namespace wok
