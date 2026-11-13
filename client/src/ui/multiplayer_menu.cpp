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
constexpr size_t kMaxServers = 6;

const char* rejectText(RejectReason r) {
    switch (r) {
        case RejectReason::ServerFull: return "Serveur plein";
        case RejectReason::VersionMismatch: return "Version du mod différente du serveur";
        case RejectReason::Banned: return "Vous êtes banni de ce serveur";
        case RejectReason::BadAuth: return "Authentification Steam refusée";
        default: return "Refusé";
    }
}

// Name of a widget we own if `w` is one of them. Looked up by name, so a stale pointer is never
// dereferenced: MyGUI widgets are destroyed with their layout.
std::string ownedName(void* w, size_t serverCount) {
    if (!w) return {};
    for (const char* n : {kButton, kConnect, kClose})
        if (mygui::findWidget(n) == w) return n;
    for (size_t i = 0; i < serverCount; ++i) {
        std::string n = kServerPrefix + std::to_string(i);
        if (mygui::findWidget(n) == w) return n;
    }
    return {};
}

}  // namespace

MultiplayerMenu::MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig)
    : cfg_(cfg), net_(net), saveConfig_(std::move(saveConfig)) {}

void MultiplayerMenu::ensureButton() {
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
    label("Adresse du serveur", 0.03f);
    auto edit = mygui::createChild(w, "EditBox", "Kenshi_EditBox", {0.05f, 0.10f, 0.90f, 0.08f}, kAddress);
    mygui::setProperty(edit, "Caption", cfg_.host + ":" + std::to_string(cfg_.port));

    label("Serveurs disponibles", 0.22f);
    size_t n = std::min(cfg_.servers.size(), kMaxServers);
    for (size_t i = 0; i < n; ++i) {
        auto b = mygui::createChild(w, "Button", "Kenshi_Button2", {0.05f, 0.29f + 0.075f * i, 0.90f, 0.065f},
                                    kServerPrefix + std::to_string(i));
        mygui::setProperty(b, "Caption", cfg_.servers[i].name + "  (" + cfg_.servers[i].address + ")");
    }
    if (n == 0) label("(aucun serveur dans wok_client.ini)", 0.29f);

    mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText", {0.05f, 0.76f, 0.90f, 0.06f}, kStatus);
    auto connect = mygui::createChild(w, "Button", "Kenshi_Button1", {0.05f, 0.85f, 0.42f, 0.10f}, kConnect);
    mygui::setProperty(connect, "Caption", "Se connecter");
    auto close = mygui::createChild(w, "Button", "Kenshi_Button1", {0.53f, 0.85f, 0.42f, 0.10f}, kClose);
    mygui::setProperty(close, "Caption", "Fermer");
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
    } else if (name == kConnect) {
        auto addr = parseAddress(mygui::editText(mygui::findWidget(kAddress)));
        if (!addr) {
            error_ = "Adresse invalide (exemple : 45.32.1.2:27800)";
            return;
        }
        error_.clear();
        cfg_.host = addr->host;
        cfg_.port = addr->port;
        saveConfig_();
        log("ui: connecting to %s:%u", cfg_.host.c_str(), cfg_.port);
        net_.start(cfg_);
    } else if (name.rfind(kServerPrefix, 0) == 0) {
        // Selecting a server connects to it right away (the address field shows which one).
        size_t i = std::stoul(name.substr(std::string(kServerPrefix).size()));
        if (i < cfg_.servers.size()) {
            mygui::setProperty(mygui::findWidget(kAddress), "Caption", cfg_.servers[i].address);
            onClick(kConnect);
        }
    }
}

void MultiplayerMenu::refreshStatus() {
    std::string s;
    if (!error_.empty()) {
        s = error_;
    } else {
        switch (net_.status()) {
            case ClientNet::Status::Disconnected: s = "Non connecté"; break;
            case ClientNet::Status::Connecting: s = "Connexion..."; break;
            case ClientNet::Status::Connected:
                s = "Connecté (joueur " + std::to_string(net_.playerId()) + ") - lance ou charge une partie";
                break;
            case ClientNet::Status::Rejected: s = std::string("Refusé : ") + rejectText(net_.rejectReason()); break;
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
        if (windowOpen_ && !mygui::findWidget(kWindow)) windowOpen_ = false;
    }
    if (windowOpen_) {
        statusTimer_ += dt;
        if (statusTimer_ >= 0.25f) {
            statusTimer_ = 0;
            refreshStatus();
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
