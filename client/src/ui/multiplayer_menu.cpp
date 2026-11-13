#include "multiplayer_menu.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

namespace wok {

namespace {

constexpr const char* kButton = "WokMultiplayerButton";
constexpr const char* kWindow = "WokMultiplayerWindow";
constexpr const char* kConnect = "WokConnect";
constexpr const char* kClose = "WokClose";
constexpr const char* kStatus = "WokStatus";
constexpr const char* kServerPrefix = "WokServer";
constexpr const char* kUnlink = "WokUnlink";
constexpr const char* kDisconnect = "WokDisconnect";
constexpr const char* kLoadingPrefix = "WokLoading_";
constexpr const char* kLoadingRoot = "WokLoading_Root";
constexpr float kLoadingTimeoutSec = 60.0f;  // never leave a player stuck behind the cover
constexpr size_t kMaxRows = 8;

// Colours ("R G B", 0-1): light text on a paler panel, for contrast with Kenshi's dark window.
constexpr const char* kHeaderText = "1 0.92 0.72";
constexpr const char* kRowText = "0.96 0.92 0.84";
constexpr const char* kStatusText = "0.96 0.92 0.84";
constexpr const char* kSeparator = "0.85 0.76 0.6";

// Server browser columns, as fractions of the list width (Aeglen's mockup).
struct Column {
    const char* suffix;
    const char* header;
    float left, width;
    const char* align;  // MyGUI TextAlign
};
constexpr Column kColumns[] = {
    {"_ver", "Ver.", 0.00f, 0.07f, "Center"},
    {"_name", "Server name", 0.07f, 0.23f, "Left VCenter"},
    {"_desc", "Server description", 0.30f, 0.46f, "Left VCenter"},
    {"_players", "Players", 0.76f, 0.12f, "Center"},
    {"_ping", "Ping", 0.88f, 0.12f, "Center"},
};
// One grid for the whole window: equal side margins, rows flush under the header (no gaps), so
// every separator sits exactly on a cell border.
constexpr float kMargin = 0.025f;
constexpr float kListLeft = kMargin, kListWidth = 1.0f - 2 * kMargin;
constexpr float kHeaderTop = 0.02f, kHeaderHeight = 0.06f;
constexpr float kRowTop = kHeaderTop + kHeaderHeight, kRowHeight = 0.065f, kRowStep = kRowHeight;
constexpr float kCellPad = 0.008f;  // text inset inside a cell

const char* rejectText(RejectReason r) {
    switch (r) {
        case RejectReason::ServerFull: return "Server full";
        case RejectReason::VersionMismatch: return "Mod version differs from the server";
        case RejectReason::Banned: return "You are banned from this server";
        case RejectReason::BadAuth: return "Steam authentication refused";
        default: return "Refused";
    }
}

std::string rowName(size_t i) { return kServerPrefix + std::to_string(i); }

// Name of a widget we own if `w` is one of them. Looked up by name, so a stale pointer is never
// dereferenced: MyGUI widgets are destroyed with their layout.
std::string ownedName(void* w, size_t rows) {
    if (!w) return {};
    for (const char* n : {kButton, kConnect, kClose, kUnlink, kDisconnect})
        if (mygui::findWidget(n) == w) return n;
    for (size_t i = 0; i < rows; ++i)
        if (mygui::findWidget(rowName(i)) == w) return rowName(i);
    return {};
}

}  // namespace

MultiplayerMenu::MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig,
                                 std::function<void()> connect, std::function<void()> unlink,
                                 std::function<bool()> inGame, std::function<void()> disconnect)
    : cfg_(cfg),
      net_(net),
      saveConfig_(std::move(saveConfig)),
      connect_(std::move(connect)),
      unlink_(std::move(unlink)),
      inGame_(std::move(inGame)),
      disconnect_(std::move(disconnect)) {}

void MultiplayerMenu::applyPauseMenu() {
    mygui::Widget* resume = mygui::findBySuffix("_ResumeButton", 3);  // Kenshi_MainMenuPopupPanel.layout
    if (!resume) return;                                                 // pause menu not open
    // A world started from multiplayer never touches local saves, even after a disconnect (the exit
    // dialog would otherwise reveal Save again): hide New/Save/Load until back on the title screen.
    bool mp = multiplayerWorld_;
    for (const char* suffix : {"_NewGameButton", "_SaveGameButton", "_LoadGameButton"})
        if (mygui::Widget* b = mygui::findBySuffix(suffix, 3); b && mygui::parentOf(b) == mygui::parentOf(resume))
            mygui::setVisible(b, !mp);
    // Disconnect stays available for the whole multiplayer world: the player may cancel Kenshi's
    // exit dialog after clicking it and must still be able to leave later.
    mygui::Widget* disc = mygui::findWidget(kDisconnect);
    if (mp && !disc) {
        // Same slot and style as Kenshi's "Save Game" button.
        disc = mygui::createChild(mygui::parentOf(resume), "Button", "Kenshi_Button1",
                                  {0.125f, 0.188953f, 0.75f, 0.130814f}, kDisconnect);
        mygui::setProperty(disc, "Caption", "Disconnect");
    } else if (!mp && disc) {
        mygui::destroy(disc);
    }
}

void MultiplayerMenu::ensureButton() {
    if (!inGame_() && mygui::findBySuffix("_NewGameButton", 3) && net_.status() != ClientNet::Status::Connected)
        multiplayerWorld_ = false;  // back on the title screen: the multiplayer world is gone
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
    mygui::Widget* w = mygui::createRoot("Window", "Kenshi_WindowCX", {0.20f, 0.15f, 0.60f, 0.70f}, "Popup", kWindow);
    if (!w) return;
    mygui::setProperty(w, "Caption", "Multiplayer");

    // Paler panel behind the server list, a slightly darker header band, light text for contrast.
    const float listBottom = kRowTop + kRowStep * kMaxRows;
    auto fill = [&](mygui::FloatCoord at, const char* colour, const char* alpha) {
        auto f = mygui::createChild(w, "Widget", "WhiteSkin", at, "");
        mygui::setProperty(f, "Colour", colour);
        mygui::setProperty(f, "Alpha", alpha);
        mygui::setProperty(f, "NeedMouse", "false");
    };
    fill({kListLeft, kHeaderTop, kListWidth, listBottom - kHeaderTop}, "0.62 0.55 0.43", "0.35");  // list background
    fill({kListLeft, kHeaderTop, kListWidth, kHeaderHeight}, "0.35 0.30 0.22", "0.75");            // header band

    for (const Column& c : kColumns) {
        auto h = mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText",
                                    {kListLeft + (c.left + kCellPad) * kListWidth, kHeaderTop,
                                     (c.width - 2 * kCellPad) * kListWidth, kHeaderHeight},
                                    "");
        mygui::setProperty(h, "Caption", c.header);
        mygui::setProperty(h, "TextAlign", c.align);
        mygui::setProperty(h, "TextColour", kHeaderText);
    }
    // Separators: vertical lines between columns, horizontal lines under the header and each row.
    auto line = [&](mygui::FloatCoord at) {
        auto l = mygui::createChild(w, "Widget", "WhiteSkin", at, "");
        mygui::setProperty(l, "Colour", kSeparator);
        mygui::setProperty(l, "Alpha", "0.55");
        mygui::setProperty(l, "NeedMouse", "false");
    };
    for (size_t c = 1; c < std::size(kColumns); ++c)
        line({kListLeft + kColumns[c].left * kListWidth, kHeaderTop, 0.0015f, listBottom - kHeaderTop});
    line({kListLeft, kRowTop, kListWidth, 0.002f});
    rowCount_ = std::min(cfg_.servers.size(), kMaxRows);
    for (size_t i = 0; i < kMaxRows; ++i)
        line({kListLeft, kRowTop + kRowStep * (i + 1), kListWidth, 0.0015f});

    for (size_t i = 0; i < rowCount_; ++i) {
        auto row = mygui::createChild(w, "Button", "Kenshi_Button2",
                                      {kListLeft, kRowTop + kRowStep * i, kListWidth, kRowHeight}, rowName(i));
        for (const Column& c : kColumns) {
            auto cell = mygui::createChild(row, "TextBox", "Kenshi_TextboxStandardText",
                                           {c.left + kCellPad, 0.0f, c.width - 2 * kCellPad, 1.0f}, rowName(i) + c.suffix);
            mygui::setProperty(cell, "TextAlign", c.align);
            mygui::setProperty(cell, "NeedMouse", "false");  // clicks go to the row button
            mygui::setProperty(cell, "TextColour", kRowText);
        }
    }
    if (rowCount_ == 0) {
        auto t = mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText", {kListLeft, kRowTop, kListWidth, 0.06f}, "");
        mygui::setProperty(t, "Caption", "(no server listed in wok_client.ini)");
        mygui::setProperty(t, "TextColour", kRowText);
    }

    // Bottom: status + Unlink on one line, then Connect / Close of equal width, same margins as the list.
    const float half = (kListWidth - kMargin) / 2;
    const float infoTop = listBottom + 0.03f, buttonsTop = infoTop + 0.10f;
    auto status = mygui::createChild(w, "TextBox", "Kenshi_TextboxStandardText", {kListLeft, infoTop, half, 0.07f}, kStatus);
    mygui::setProperty(status, "TextAlign", "Left VCenter");
    mygui::setProperty(status, "TextColour", kStatusText);
    // Unlink lives here, not in Kenshi's Options: Kenshi casts every Options tab to its own panel
    // class when applying settings and crashes on a foreign tab (MyGUI "Bad cast ... DatapanelGUI").
    auto unlink = mygui::createChild(w, "Button", "Kenshi_Button2", {kListLeft + half + kMargin, infoTop, half, 0.07f}, kUnlink);
    mygui::setProperty(unlink, "Caption", "Unlink Steam account");
    auto connect = mygui::createChild(w, "Button", "Kenshi_Button1", {kListLeft, buttonsTop, half, 0.10f}, kConnect);
    mygui::setProperty(connect, "Caption", "Connect");
    auto close = mygui::createChild(w, "Button", "Kenshi_Button1", {kListLeft + half + kMargin, buttonsTop, half, 0.10f}, kClose);
    mygui::setProperty(close, "Caption", "Close");

    windowOpen_ = true;
    lastStatus_ = "?";
    error_.clear();
    browser_.start(std::vector<ServerEntry>(cfg_.servers.begin(), cfg_.servers.begin() + rowCount_));
    // Preselect the last server used.
    selected_ = -1;
    for (size_t i = 0; i < rowCount_; ++i)
        if (auto a = parseAddress(cfg_.servers[i].address); a && a->host == cfg_.host && a->port == cfg_.port)
            select(static_cast<int>(i));
    refreshRows();
}

void MultiplayerMenu::closeWindow() {
    browser_.stop();
    mygui::destroy(mygui::findWidget(kWindow));
    windowOpen_ = false;
}

void MultiplayerMenu::select(int row) {
    selected_ = row;
    for (size_t i = 0; i < rowCount_; ++i)
        mygui::setProperty(mygui::findWidget(rowName(i)), "StateSelected", static_cast<int>(i) == row ? "true" : "false");
}

void MultiplayerMenu::refreshRows() {
    auto rows = browser_.rows();
    for (size_t i = 0; i < rows.size() && i < rowCount_; ++i) {
        const ServerRow& r = rows[i];
        auto set = [&](const char* suffix, const std::string& text) {
            mygui::setProperty(mygui::findWidget(rowName(i) + suffix), "Caption", text);
        };
        bool online = r.state == ServerRow::State::Online && r.info;
        set("_ver", online ? "v" + std::to_string(r.info->protocolVersion) : "-");
        set("_name", online ? r.info->name : r.entry.name);
        set("_desc", online ? r.info->description
                            : (r.state == ServerRow::State::Offline ? "Offline (" + r.entry.address + ")" : "Querying..."));
        set("_players", online ? std::to_string(r.info->players) + " / " + std::to_string(r.info->maxPlayers) : "-");
        set("_ping", online ? std::to_string(r.pingMs) + " ms" : "-");
    }
}

void MultiplayerMenu::onClick(const std::string& name) {
    if (name == kButton) {
        openWindow();
    } else if (name == kClose) {
        closeWindow();
    } else if (name == kDisconnect) {
        // Ask first: Kenshi's own exit dialog. We only leave the server if the player confirms.
        log("ui: Disconnect asked, waiting for the exit confirmation");
        disconnectPending_ = true;
        if (mygui::Widget* exitGame = mygui::findBySuffix("_ExitGameButton", 3)) mygui::click(exitGame);
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
        if (selected_ < 0 || static_cast<size_t>(selected_) >= rowCount_) {
            error_ = "Select a server first.";
            return;
        }
        auto addr = parseAddress(cfg_.servers[selected_].address);
        if (!addr) {
            error_ = "Invalid server address in wok_client.ini.";
            return;
        }
        error_.clear();
        cfg_.host = addr->host;
        cfg_.port = addr->port;
        saveConfig_();
        log("ui: connecting to %s:%u", cfg_.host.c_str(), cfg_.port);
        connect_();
    } else if (name.rfind(kServerPrefix, 0) == 0) {
        select(static_cast<int>(std::stoul(name.substr(std::string(kServerPrefix).size()))));
        error_.clear();
    }
}

void MultiplayerMenu::showLoading(const std::string& text) {
    if (!loadingShown_) {
        // Kenshi's own loading screen (same layout file the game uses), under our widget prefix.
        if (mygui::loadLayout("Kenshi_LoadingScreen.layout", kLoadingPrefix) && mygui::findWidget(kLoadingRoot)) {
            static const char* kImages[] = {"loading_screen0.dds", "loading_screen1.dds", "loading_screen2.dds",
                                            "loading_screen3.dds", "loading_screen4.dds", "loading_screen5.dds",
                                            "loading_screen6.dds", "loading_screen9.dds", "loading_screen10.dds"};
            const char* image = kImages[GetTickCount64() % std::size(kImages)];
            mygui::setProperty(mygui::findWidget(std::string(kLoadingPrefix) + "BackgroundImage"), "ImageTexture", image);
            mygui::setProperty(mygui::findWidget(std::string(kLoadingPrefix) + "MessageText"), "Caption",
                               "Joining World of Kenshi - your character is on its way.");
            log("ui: Kenshi loading screen shown (%s)", image);
        } else {
            // Fallback: plain opaque cover on the topmost layer.
            mygui::Widget* cover = mygui::createRoot("Widget", "WhiteSkin", {0.0f, 0.0f, 1.0f, 1.0f}, "Top", kLoadingRoot);
            if (!cover) return;
            mygui::setProperty(cover, "Colour", "0.09 0.08 0.06");
            auto line = mygui::createChild(cover, "TextBox", "Kenshi_TextboxStandardText", {0.0f, 0.50f, 1.0f, 0.06f},
                                           std::string(kLoadingPrefix) + "LoadingText");
            mygui::setProperty(line, "TextAlign", "Center");
            mygui::setProperty(line, "TextColour", kRowText);
            log("ui: fallback loading cover shown");
        }
        loadingShown_ = true;
        loadingTime_ = 0;
    }
    if (text != loadingText_) {
        mygui::setProperty(mygui::findWidget(std::string(kLoadingPrefix) + "LoadingText"), "Caption", text);
        loadingText_ = text;
    }
}

void MultiplayerMenu::hideLoading() {
    if (mygui::Widget* cover = mygui::findWidget(kLoadingRoot)) mygui::destroy(cover);
    if (loadingShown_) log("ui: loading screen hidden after %.1f s", loadingTime_);
    loadingShown_ = false;
    loadingText_.clear();
}

void MultiplayerMenu::updateLoading(float dt) {
    bool returning = net_.status() == ClientNet::Status::Connected && net_.hasCharacter() && enteredGame_;
    if (!returning) {
        if (loadingShown_) hideLoading();
        worldReady_ = false;
        return;
    }
    if (worldReady_ || (loadingShown_ && loadingTime_ > kLoadingTimeoutSec)) {
        if (loadingShown_) hideLoading();
        return;
    }
    loadingTime_ += dt;
    const char* step = autoStep_ == 0 ? "Joining the server..." : autoStep_ == 1 ? "Loading the world..."
                                                                                : "Restoring your character...";
    // Never two loading screens: while Kenshi shows its own, drop ours and write our step into it.
    if (mygui::Widget* kenshiText = mygui::findBySuffixExcept("_LoadingText", kLoadingPrefix, 4)) {
        if (mygui::Widget* ours = mygui::findWidget(kLoadingRoot)) mygui::destroy(ours);
        mygui::setProperty(kenshiText, "Caption", step);
        loadingShown_ = true;  // still "covered", by Kenshi's screen
        loadingText_ = step;
        return;
    }
    if (loadingShown_ && !mygui::findWidget(kLoadingRoot)) loadingShown_ = false;  // Kenshi's screen just closed
    showLoading(step);
}

void MultiplayerMenu::applyConnectedMenu() {
    if (net_.status() != ClientNet::Status::Connected) {
        enteredGame_ = false;
        autoStep_ = 0;
        return;
    }
    // Returning player: Kenshi's start and character-editor screens only load the world, so walk
    // through them automatically (the server character replaces the start one once in game).
    // (During the character editor the world already exists, so this must not depend on inGame_.)
    if (enteredGame_ && net_.hasCharacter() && autoStep_ < 2 && (autoStep_ == 1 || !inGame_())) {
        if (autoStep_ == 0) {
            if (mygui::Widget* begin = mygui::findBySuffix("_BeginButton", 6)) {
                autoStep_ = 1;
                confirmWaits_ = 0;
                log("ui: returning player, starting the world (Begin)");
                mygui::click(begin);
            }
        } else if (autoStep_ == 1) {
            // The editor's Confirm only works once the editor has finished building: keep clicking it
            // every ~2 s while it exists; the editor is done once the button is gone.
            mygui::Widget* confirm = mygui::findBySuffixExcept("_ConfirmButton", kLoadingPrefix, 12);
            if (confirm) {
                if (confirmWaits_++ % 4 == 0) {
                    log("ui: returning player, confirming the character editor ('%s')", mygui::nameOf(confirm).c_str());
                    mygui::click(confirm);
                }
            } else if (confirmWaits_ > 0) {
                autoStep_ = 2;
                log("ui: character editor closed");
            }
        }
        return;
    }
    if (enteredGame_ || inGame_()) return;
    mygui::Widget* newGame = mygui::findBySuffix("_NewGameButton", 3);
    if (!newGame) return;  // not on the title screen
    // Step 1 of entering the server game: let Kenshi run its own New Game action.
    closeWindow();
    enteredGame_ = true;
    multiplayerWorld_ = true;
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
        applyPauseMenu();
        updateLoading(0.5f);
        if (windowOpen_ && !mygui::findWidget(kWindow)) {  // window closed by the game (e.g. menu rebuilt)
            browser_.stop();
            windowOpen_ = false;
        }
        if (windowOpen_) refreshRows();
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
    if (down && !mouseWasDown_) {
        pressedOn_ = mygui::mouseFocus();
        if (disconnectPending_ && pressedOn_) {
            // Kenshi's exit dialog: ButtonA = Yes, ButtonC = No (hover recon). Act on the press,
            // before the game handles the click and closes.
            std::string n = mygui::nameOf(static_cast<mygui::Widget*>(pressedOn_));
            auto endsWith = [&](const char* suf) {
                size_t l = std::strlen(suf);
                return n.size() >= l && n.compare(n.size() - l, l, suf) == 0;
            };
            if (endsWith("_ButtonA")) {
                log("ui: exit confirmed, disconnecting from the server");
                disconnectPending_ = false;
                disconnect_();
            } else if (endsWith("_ButtonC")) {
                log("ui: exit cancelled, staying connected");
                disconnectPending_ = false;
            }
        }
    }
    if (!down && mouseWasDown_) {
        void* releasedOn = mygui::mouseFocus();
        if (releasedOn && releasedOn == pressedOn_) {
            std::string name = ownedName(releasedOn, rowCount_);
            if (!name.empty()) onClick(name);
        }
        pressedOn_ = nullptr;
    }
    mouseWasDown_ = down;
}

}  // namespace wok
