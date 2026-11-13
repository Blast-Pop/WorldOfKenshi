#include "multiplayer_menu.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <algorithm>
#include <string>

namespace wok {

namespace {

constexpr const char* kButton = "WokMultiplayerButton";
constexpr const char* kWindow = "WokMultiplayerWindow";
constexpr const char* kAddress = "WokAddress";
constexpr const char* kConnect = "WokConnect";
constexpr const char* kClose = "WokClose";
constexpr const char* kStatus = "WokStatus";
constexpr const char* kServerPrefix = "WokServer";
constexpr const char* kUnlink = "WokUnlink";
constexpr size_t kMaxServers = 6;

const char* rejectText(RejectReason r) {
    switch (r) {
        case RejectReason::ServerFull: return "Server full";
        case RejectReason::VersionMismatch: return "Mod version differs from the server";
        case RejectReason::Banned: return "You are banned from this server";
        case RejectReason::BadAuth: return "Steam authentication refused";
        default: return "Refused";
    }
}

// Name of a widget we own if `w` is one of them. Looked up by name, so a stale pointer is never
// dereferenced: MyGUI widgets are destroyed with their layout.
std::string ownedName(void* w, size_t serverCount) {
    if (!w) return {};
    for (const char* n : {kButton, kConnect, kClose, kUnlink})
        if (mygui::findWidget(n) == w) return n;
    for (size_t i = 0; i < serverCount; ++i) {
        std::string n = kServerPrefix + std::to_string(i);
        if (mygui::findWidget(n) == w) return n;
    }
    return {};
}

}  // namespace

MultiplayerMenu::MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig,
                                 std::function<void()> connect, std::function<void()> unlink,
                                 std::function<bool()> inGame)
    : cfg_(cfg),
      net_(net),
      saveConfig_(std::move(saveConfig)),
      connect_(std::move(connect)),
      unlink_(std::move(unlink)),
      inGame_(std::move(inGame)) {}

void MultiplayerMenu::ensureButton() {
    if (inGame_()) {  // pause menu: no Multiplayer button there
        if (mygui::Widget* b = mygui::findWidget(kButton)) mygui::destroy(b);
        return;
    }
    if (mygui::findWidget(kButton)) return;
    // Kenshi prefixes layout widget names, so match "_NewGameButton" (always present, unlike Continue).
    mygui::Widget* root = mygui::parentOf(mygui::findBySuffix("_NewGameButton", 3));
    if (!root) return;  // main menu not shown
    // Same column and size as Kenshi's buttons, one slot above "Continue" (Kenshi_MainMenu.layout).
    mygui::Widget* b =
        mygui::createChild(root, "Button", "Kenshi_Button1", {0.260417f, 0.027778f, 0.15625f, 0.0638889f}, kButton);
    mygui::setProperty(b, "Caption", "Multiplayer");
    log("ui: Multiplayer button added to the main menu");
}

void MultiplayerMenu::openWindow() {
    if (mygui::findWidget(kWindow)) return;
    mygui::Widget* w = mygui::createRoot("Window", "Kenshi_WindowCX", {0.30f, 0.20f, 0.40f, 0.60f}, "Popup", kWindow);
    if (!w) return;
    mygui::setProperty(w, "Caption", "Multiplayer");

    auto label = [&](const std::string& text, float top) {
        auto t = mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText", {0.05f, top, 0.90f, 0.06f}, "");
        mygui::setProperty(t, "Caption", text);
    };
    label("Server address", 0.03f);
    auto edit = mygui::createChild(w, "EditBox", "Kenshi_EditBox", {0.05f, 0.10f, 0.90f, 0.08f}, kAddress);
    mygui::setProperty(edit, "Caption", cfg_.host + ":" + std::to_string(cfg_.port));

    label("Available servers", 0.22f);
    size_t n = std::min(cfg_.servers.size(), kMaxServers);
    for (size_t i = 0; i < n; ++i) {
        auto b = mygui::createChild(w, "Button", "Kenshi_Button2", {0.05f, 0.29f + 0.075f * i, 0.90f, 0.065f},
                                    kServerPrefix + std::to_string(i));
        mygui::setProperty(b, "Caption", cfg_.servers[i].name + "  (" + cfg_.servers[i].address + ")");
    }
    if (n == 0) label("(no server listed in wok_client.ini)", 0.29f);

    mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText", {0.05f, 0.76f, 0.46f, 0.06f}, kStatus);
    // Unlink lives here, not in Kenshi's Options: Kenshi casts every Options tab to its own panel
    // class when applying settings and crashes on a foreign tab (MyGUI "Bad cast ... DatapanelGUI").
    auto unlink = mygui::createChild(w, "Button", "Kenshi_Button2", {0.53f, 0.75f, 0.42f, 0.08f}, kUnlink);
    mygui::setProperty(unlink, "Caption", "Unlink Steam account");
    auto connect = mygui::createChild(w, "Button", "Kenshi_Button1", {0.05f, 0.85f, 0.42f, 0.10f}, kConnect);
    mygui::setProperty(connect, "Caption", "Connect");
    auto close = mygui::createChild(w, "Button", "Kenshi_Button1", {0.53f, 0.85f, 0.42f, 0.10f}, kClose);
    mygui::setProperty(close, "Caption", "Close");
    windowOpen_ = true;
    lastStatus_ = "?";
    error_.clear();
}

void MultiplayerMenu::closeWindow() {
    mygui::destroy(mygui::findWidget(kWindow));
    windowOpen_ = false;
}

void MultiplayerMenu::onClick(const std::string& name) {
    if (name == kButton) {
        openWindow();
    } else if (name == kClose) {
        closeWindow();
    } else if (name == kUnlink) {
        unlink_();
        error_ = "Account unlinked: the next connection will ask Steam again.";
    } else if (name == kConnect) {
        auto st = net_.status();
        if (st == ClientNet::Status::Connecting || st == ClientNet::Status::AwaitingSteamLogin ||
            st == ClientNet::Status::Connected) {
            log("ui: Connect ignored, a connection is already in progress");
            return;  // never restart a connection (and a Steam login) that is still going
        }
        auto addr = parseAddress(mygui::editText(mygui::findWidget(kAddress)));
        if (!addr) {
            error_ = "Invalid address (example: 45.32.1.2:27800)";
            return;
        }
        error_.clear();
        cfg_.host = addr->host;
        cfg_.port = addr->port;
        saveConfig_();
        log("ui: connecting to %s:%u", cfg_.host.c_str(), cfg_.port);
        connect_();
    } else if (name.rfind(kServerPrefix, 0) == 0) {
        // Selecting a server only fills the address; "Connect" does the rest.
        size_t i = std::stoul(name.substr(std::string(kServerPrefix).size()));
        if (i < cfg_.servers.size())
            mygui::setProperty(mygui::findWidget(kAddress), "Caption", cfg_.servers[i].address);
    }
}

void MultiplayerMenu::applyConnectedMenu() {
    if (net_.status() != ClientNet::Status::Connected) {
        enteredGame_ = false;
        return;
    }
    if (enteredGame_ || inGame_()) return;
    mygui::Widget* newGame = mygui::findBySuffix("_NewGameButton", 3);
    if (!newGame) return;  // not on the title screen
    // Step 1 of entering the server game: let Kenshi run its own New Game action.
    closeWindow();
    enteredGame_ = true;
    log("ui: connected, starting the server game (New Game)");
    mygui::click(newGame);
}

void MultiplayerMenu::refreshStatus() {
    std::string s;
    if (!error_.empty()) {
        s = error_;
    } else {
        switch (net_.status()) {
            case ClientNet::Status::Disconnected: s = "Not connected"; break;
            case ClientNet::Status::Connecting: s = "Connecting..."; break;
            case ClientNet::Status::AwaitingSteamLogin:
                s = "Steam login in progress in your browser (steamcommunity.com tab)";
                break;
            case ClientNet::Status::Connected:
                s = std::string("Connected - ") + (net_.hasCharacter() ? "character found" : "no character yet");
                break;
            case ClientNet::Status::Rejected: s = std::string("Refused: ") + rejectText(net_.rejectReason()); break;
        }
    }
    if (s != lastStatus_) {
        mygui::setProperty(mygui::findWidget(kStatus), "Caption", s);
        lastStatus_ = s;
    }
}

void MultiplayerMenu::update(float dt) {
    if (!mygui::available()) return;
    pollTimer_ += dt;
    if (pollTimer_ >= 0.5f) {
        pollTimer_ = 0;
        ensureButton();
        applyConnectedMenu();
        if (windowOpen_ && !mygui::findWidget(kWindow)) windowOpen_ = false;
    }
    if (windowOpen_) {
        statusTimer_ += dt;
        if (statusTimer_ >= 0.25f) {
            statusTimer_ = 0;
            refreshStatus();
        }
    }

    // Dev diagnostic while automating the entry flow: log the names of hovered widgets.
    static void* lastHover = nullptr;
    static int hoverLogs = 0;
    if (void* hover = mygui::mouseFocus(); hover != lastHover) {
        lastHover = hover;
        if (hover && hoverLogs < 120) {
            ++hoverLogs;
            log("ui: hover '%s'", mygui::nameOf(static_cast<mygui::Widget*>(hover)).c_str());
        }
    }

    // A click is the left button pressed and released over the same widget we own.
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (down && !mouseWasDown_) pressedOn_ = mygui::mouseFocus();
    if (!down && mouseWasDown_) {
        void* releasedOn = mygui::mouseFocus();
        if (releasedOn && releasedOn == pressedOn_) {
            std::string name = ownedName(releasedOn, std::min(cfg_.servers.size(), kMaxServers));
            if (!name.empty()) onClick(name);
        }
        pressedOn_ = nullptr;
    }
    mouseWasDown_ = down;
}

}  // namespace wok
