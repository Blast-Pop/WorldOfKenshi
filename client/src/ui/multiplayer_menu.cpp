#include "multiplayer_menu.hpp"

#include "../game/game_memory.hpp"
#include "../game/identity.hpp"
#include "../input.hpp"

#include "../log.hpp"
#include "mygui_abi.hpp"

#include <windows.h>

#include <core/appearance.hpp>
#include <core/paths.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
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
constexpr const char* kDelete = "WokDeleteCharacter";
constexpr const char* kDeathWindow = "WokDeathWindow";
constexpr const char* kDeathNew = "WokDeathNew";
constexpr const char* kDeathExit = "WokDeathExit";
constexpr const char* kLoadingPrefix = "WokLoading_";
constexpr const char* kLoadingRoot = "WokLoadingCover";
constexpr float kLoadingTimeoutSec = 75.0f;  // never leave a player stuck behind the cover
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
    for (const char* n : {kButton, kConnect, kClose, kUnlink, kDelete, kDeathNew, kDeathExit})
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
    bool mp = multiplayerWorld_ && !newGameAsked_;  // after a death, New Game is the way on
    for (const char* suffix : {"_NewGameButton", "_SaveGameButton", "_LoadGameButton"})
        if (mygui::Widget* b = mygui::findBySuffix(suffix, 3); b && mygui::parentOf(b) == mygui::parentOf(resume))
            mygui::setVisible(b, !mp);
    // Kenshi cannot return to the title screen from a world, so leaving means quitting: in a
    // multiplayer world "Exit Game" becomes "Disconnect" (same button, same exit dialog), and the
    // server connection is closed when the player confirms.
    if (mygui::Widget* exitGame = mygui::findBySuffix("_ExitGameButton", 3);
        exitGame && mygui::parentOf(exitGame) == mygui::parentOf(resume) && mp != exitRenamed_) {
        mygui::setProperty(exitGame, "Caption", mp ? "Disconnect" : "Exit Game");
        exitRenamed_ = mp;
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
    master_.start(cfg_.masterUrl);  // the server list is ready by the time the window opens
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
    // Servers from wok_client.ini first, then every server announced to the master list.
    listed_ = cfg_.servers;
    for (const ServerEntry& m : master_.servers()) {
        auto ma = parseAddress(m.address);
        bool known = false;
        for (const ServerEntry& e : listed_)
            if (auto ea = parseAddress(e.address); ea && ma && ea->host == ma->host && ea->port == ma->port) known = true;
        if (!known) listed_.push_back(m);
    }
    master_.start(cfg_.masterUrl);  // refresh for the next time the window opens
    rowCount_ = std::min(listed_.size(), kMaxRows);
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
        mygui::setProperty(t, "Caption", "(no server found: master list unreachable and none in wok_client.ini)");
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
    const float quarter = (half - kMargin) / 2;
    auto unlink = mygui::createChild(w, "Button", "Kenshi_Button2", {kListLeft + half + kMargin, infoTop, quarter, 0.07f}, kUnlink);
    mygui::setProperty(unlink, "Caption", "Unlink Steam");
    auto del = mygui::createChild(w, "Button", "Kenshi_Button2",
                                  {kListLeft + half + 2 * kMargin + quarter, infoTop, quarter, 0.07f}, kDelete);
    mygui::setProperty(del, "Caption", "Delete character");
    auto connect = mygui::createChild(w, "Button", "Kenshi_Button1", {kListLeft, buttonsTop, half, 0.10f}, kConnect);
    mygui::setProperty(connect, "Caption", "Connect");
    auto close = mygui::createChild(w, "Button", "Kenshi_Button1", {kListLeft + half + kMargin, buttonsTop, half, 0.10f}, kClose);
    mygui::setProperty(close, "Caption", "Close");

    windowOpen_ = true;
    lastStatus_ = "?";
    error_.clear();
    browser_.start(std::vector<ServerEntry>(listed_.begin(), listed_.begin() + rowCount_));
    // Preselect the last server used.
    selected_ = -1;
    for (size_t i = 0; i < rowCount_; ++i)
        if (auto a = parseAddress(listed_[i].address); a && a->host == cfg_.host && a->port == cfg_.port)
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

void MultiplayerMenu::showDeathWindow() {
    mygui::destroy(mygui::findWidget(kDeathWindow));
    mygui::Widget* w = mygui::createRoot("Window", "Kenshi_WindowCX", {0.375f, 0.40f, 0.25f, 0.20f}, "Popup", kDeathWindow);
    if (!w) return;
    mygui::setProperty(w, "Caption", "Your character has died");
    auto add = [&](const char* name, const char* caption, float left) {
        if (auto b = mygui::createChild(w, "Button", "Kenshi_Button1", {left, 0.45f, 0.40f, 0.30f}, name))
            mygui::setProperty(b, "Caption", caption);
    };
    add(kDeathNew, "New character", 0.06f);
    add(kDeathExit, "Exit", 0.54f);
    log("ui: death window shown");
}

void MultiplayerMenu::onClick(const std::string& name) {
    if (name == kDeathNew || name == kDeathExit) {
        mygui::destroy(mygui::findWidget(kDeathWindow));
        if (name == kDeathNew) {
            log("ui: death window -> New character");
            restartForNewCharacter();
        } else {
            // Kenshi cannot return to its title screen from a world: leaving means quitting.
            // Nothing is saved locally in multiplayer, so the game can close right away.
            log("ui: death window -> Exit, disconnecting and closing the game");
            disconnect_();
            Sleep(300);  // let the disconnect reach the server
            ExitProcess(0);
        }
        return;
    }
    if (name == kButton) {
        openWindow();
    } else if (name == kClose) {
        closeWindow();
    } else if (name == kUnlink) {
        unlink_();
        error_ = "Account unlinked: the next connection will ask Steam again.";
    } else if (name == kDelete) {
        if (net_.status() != ClientNet::Status::Disconnected && net_.status() != ClientNet::Status::Rejected) {
            error_ = "Already connected: delete your character from the main menu.";
            return;
        }
        if (selected_ < 0 || static_cast<size_t>(selected_) >= rowCount_) {
            error_ = "Select a server first.";
            return;
        }
        if (deleteArmedFor_ <= 0) {  // first click: ask to confirm
            deleteArmedFor_ = 6.0f;
            error_ = "Delete your character on this server for good? Click Delete character again.";
            return;
        }
        auto addr = parseAddress(listed_[selected_].address);
        if (!addr) return;
        deleteArmedFor_ = 0;
        cfg_.host = addr->host;
        cfg_.port = addr->port;
        saveConfig_();
        delete_ = Delete::Connecting;
        deleteStarted_ = false;
        error_ = "Deleting your character...";
        log("ui: deleting our character on %s:%u", cfg_.host.c_str(), cfg_.port);
        connect_();
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
        auto addr = parseAddress(listed_[selected_].address);
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
    // Development: a "debug_show_editor" file next to the log keeps the automated editor visible.
    static const bool showEditor = GetFileAttributesA(modFile(modDir(), "debug_show_editor").c_str()) !=
                                   INVALID_FILE_ATTRIBUTES;
    if (showEditor) return;
    if (!mygui::findWidget(kLoadingRoot)) {
        // Our own cover on "Pointer", Kenshi's topmost MyGUI layer (core_layer.xml order): above the
        // game's loading screen and the character editor. Sized to the real window (UI scale).
        mygui::Widget* cover = mygui::createRoot("Widget", "WhiteSkin", {0.0f, 0.0f, 1.0f, 1.0f}, "Pointer", kLoadingRoot);
        if (!cover) return;
        mygui::setProperty(cover, "Colour", "0.09 0.08 0.06");
        RECT rc{};
        int w = 0, h = 0;
        if (HWND wnd = GetActiveWindow() ? GetActiveWindow() : GetForegroundWindow(); wnd && GetClientRect(wnd, &rc)) {
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
            mygui::setCoord(cover, 0, 0, w, h);
        }
        // Inside it, Kenshi's own loading layout (from the player's install) for the familiar look.
        if (mygui::loadLayout("Kenshi_LoadingScreen.layout", kLoadingPrefix, cover)) {
            static const char* kImages[] = {"loading_screen0.dds", "loading_screen1.dds", "loading_screen2.dds",
                                            "loading_screen3.dds", "loading_screen4.dds", "loading_screen5.dds",
                                            "loading_screen6.dds", "loading_screen9.dds", "loading_screen10.dds"};
            const char* image = kImages[GetTickCount64() % std::size(kImages)];
            if (w && h) mygui::setCoord(mygui::findWidget(std::string(kLoadingPrefix) + "Root"), 0, 0, w, h);
            mygui::setProperty(mygui::findWidget(std::string(kLoadingPrefix) + "BackgroundImage"), "ImageTexture", image);
            log("ui: loading screen shown (%s)", image);
        }
        loadingShown_ = true;
        loadingTime_ = 0;
        loadingText_.clear();
        lastSlide_ = -1;
        lastTipSlot_ = -1;
        nameTries_ = 0;
        importStage_ = 0;
        importTries_ = 0;
        genderClicks_ = 0;
        listTries_ = 0;
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
    // Animated like Kenshi's own screen: dots cycling on a short title (Kenshi's big LOADING font),
    // and our own World of Kenshi tips rotating in the message box below it.
    static const char* kSteps[] = {"JOINING", "LOADING", "RESTORING"};
    static const char* kTips[] = {
        "Your character lives on the server: you will find it where you left it, even after a crash.",
        "While you are offline your character stays in the world. Choose a safe place to log out.",
        "Time never stops on a World of Kenshi server: there is no pause and no speed-up.",
        "Corpses stay for five minutes before they rot away. Loot fast.",
        "Other players are real people: trade, team up, or keep your distance.",
        "Servers can run mods. Everyone on a server uses the same mod list.",
    };
    int dots = 1 + static_cast<int>(loadingTime_ * 2.0f) % 3;
    std::string title = std::string(kSteps[std::min(autoStep_, 2)]) + std::string(dots, '.');
    // A random tip every 8 s, never the same one twice in a row.
    int tipSlot = static_cast<int>(loadingTime_ / 8.0f);
    if (tipSlot != lastTipSlot_) {
        size_t pick = static_cast<size_t>(GetTickCount64() / 7) % std::size(kTips);
        if (kTips[pick] == lastTip_) pick = (pick + 1) % std::size(kTips);
        if (mygui::Widget* msg = mygui::findWidget(std::string(kLoadingPrefix) + "MessageText")) {
            mygui::setProperty(msg, "Caption", kTips[pick]);
            lastTip_ = kTips[pick];
            lastTipSlot_ = tipSlot;
        }
    }
    const std::string& step = title;

    // Like Kenshi's loading screen: a new random background image every few seconds.
    static const char* kImages[] = {"loading_screen0.dds", "loading_screen1.dds", "loading_screen2.dds",
                                    "loading_screen3.dds", "loading_screen4.dds", "loading_screen5.dds",
                                    "loading_screen6.dds", "loading_screen9.dds", "loading_screen10.dds"};
    int slide = static_cast<int>(loadingTime_ / 6.0f);
    if (slide != lastSlide_) {
        size_t pick = static_cast<size_t>(GetTickCount64() % std::size(kImages));
        if (pick == lastImage_) pick = (pick + 1) % std::size(kImages);  // never the same twice in a row
        if (mygui::Widget* bg = mygui::findWidget(std::string(kLoadingPrefix) + "BackgroundImage"))
            mygui::setProperty(bg, "ImageTexture", kImages[pick]);
        lastImage_ = pick;
        lastSlide_ = slide;
    }

    // Ours stays up for the whole flow: Kenshi's own loading screen stops at the character editor.
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
            // Kenshi rejects a Confirm sent before the editor has assigned a race ("Invalid character
            // race" dialog, whose OK is ButtonB). Wait until the editor has been open ~4 s, click,
            // dismiss any error dialog, and retry every ~4 s (at most 6 times). Polls run every 0.5 s.
            if (mygui::Widget* ok = mygui::findBySuffixExcept("_ButtonB", kLoadingPrefix, 4)) {
                log("ui: dismissing an editor message (OK)");
                mygui::click(ok);
                return;
            }
            mygui::Widget* confirm = mygui::findBySuffixExcept("_ConfirmButton", kLoadingPrefix, 12);
            // Look: Kenshi's editor Import opens a file dialog; type our file name and open it.
            if (confirm && importStage_ == 1) {
                mygui::Widget* fileName = nullptr;
                for (mygui::Widget* f : mygui::findEditBoxesBySuffix("_EditFileName", 12))
                    if (mygui::isShown(f)) fileName = f;
                mygui::Widget* open = mygui::findShownBySuffix("_ButtonOpenSave", 12);
                mygui::Widget* files = mygui::findShownBySuffix("_ListFiles", 12);
                if (open && files) {
                    // Kenshi's dialog loads the file chosen in its list (typing the name is ignored).
                    std::string stem = importFile_.substr(0, importFile_.rfind('.'));
                    // The first press may hit a row the list has not redrawn yet after scrolling to
                    // it: retry on the next polls until our file is the selected one.
                    bool chosen = mygui::pressListItem(files, stem);
                    if (chosen || ++listTries_ >= 4) {
                        log("ui: importing the look '%s': %s", importFile_.c_str(),
                            chosen ? "selected, opening" : "NOT selected, cancelled");
                        if (chosen) mygui::click(open);
                        else if (mygui::Widget* cancel = mygui::findShownBySuffix("_ButtonCancel", 12)) mygui::click(cancel);
                        importStage_ = 2;
                    }
                }
                (void)fileName;
                return;
            }
            if (confirm) {
                ++confirmWaits_;
                if (confirmWaits_ >= 8 && confirmWaits_ % 8 == 0 && confirmWaits_ / 8 <= 12) {
                    // Kenshi names the character from what is typed in the editor's name field
                    // (NamePanel/NameText), and only real key presses reach its handler: type the
                    // server name with the keyboard, then confirm on the next try.
                    bool nameReady = true;
                    auto data = net_.character();
                    if (data && importStage_ == 0) {
                        auto look = decodeAppearance(data->appearance);
                        // Import keeps the editor's current sex (and drops hair/beard that do not fit
                        // it): set the saved sex first with the editor's own arrows.
                        auto lower = [](std::string t) {
                            for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                            return t;
                        };
                        std::string wanted = lower(look["gender"]);
                        std::string shown = lower(mygui::labelText(mygui::findShownBySuffix("_GenderText", 12)));
                        if (genderClicks_ == 0)
                            log("ui: editor sex shown '%s', saved '%s'", shown.c_str(), wanted.c_str());
                        if ((shown == "male" || shown == "female") && !wanted.empty() && shown != wanted && genderClicks_ < 4) {
                            ++genderClicks_;
                            log("ui: editor sex is '%s', switching to '%s'", shown.c_str(), wanted.c_str());
                            if (mygui::Widget* next = mygui::findShownBySuffix("_GenderRightButton", 12)) mygui::click(next);
                            return;
                        }
                        auto body = base64Decode(look["bod2"]);
                        mygui::Widget* importButton = mygui::findShownBySuffix("_ImportButton", 12);
                        // The editor sometimes shows up late: wait for it (sex label + Import button)
                        // rather than confirming a random character; give up only after ~40 s.
                        if (!body.empty() && (!importButton || shown.empty()) && confirmWaits_ / 8 < 10) {
                            log("ui: waiting for the character editor before restoring the look");
                            return;
                        }
                        importFile_ = "WoK - " + data->name + ".bod2";
                        if (!body.empty() && importButton &&
                            std::ofstream(exportedBodyPath("WoK - " + data->name), std::ios::binary)
                                .write(reinterpret_cast<const char*>(body.data()), body.size())) {
                            importStage_ = 1;
                            log("ui: restoring the look (%zu bytes), opening Import", body.size());
                            mygui::click(importButton);
                            return;
                        }
                        if (body.empty() || ++importTries_ >= 3) {
                            importStage_ = 2;  // give up on the look, keep the flow going
                            log("ui: look not restored (saved look %zu bytes, appearance %zu bytes, Import button %s)",
                                body.size(), data->appearance.size(), importButton ? "found" : "missing");
                        }
                    }
                    if (data) {
                        mygui::Widget* field = nullptr;
                        for (mygui::Widget* f : mygui::findEditBoxesBySuffix("_NameText", 12)) {
                            bool shown = mygui::isShown(f);
                            if (nameTries_ == 0)
                                log("ui: name field candidate '%s' shown=%d text='%s'", mygui::nameOf(f).c_str(),
                                    shown ? 1 : 0, mygui::editText(f));
                            if (shown && !field) field = f;
                        }
                        if (field && data->name != mygui::editText(field) && nameTries_ < 2) {
                            nameReady = false;
                            std::string now = mygui::injectText(field, data->name);
                            log("ui: typing the character name '%s' in the editor -> '%s'", data->name.c_str(),
                                now.c_str());
                            if (++nameTries_ >= 2) nameReady = true;  // do not block the flow on it
                        }
                    }
                    if (nameReady) {
                        log("ui: returning player, confirming the character editor (try %d)", confirmWaits_ / 8);
                        mygui::click(confirm);
                    }
                }
            } else if (confirmWaits_ > 0) {
                autoStep_ = 2;
                log("ui: character editor closed");
            }
        }
        return;
    }
    if (enteredGame_ || inGame_() || delete_ != Delete::Off) return;  // never enter the world to delete
    mygui::Widget* newGame = mygui::findBySuffix("_NewGameButton", 3);
    if (!newGame) return;  // not on the title screen
    // Step 1 of entering the server game: let Kenshi run its own New Game action.
    closeWindow();
    enteredGame_ = true;
    multiplayerWorld_ = true;
    log("ui: connected, starting the server game (New Game)");
    // Returning player: cover the screen before Kenshi opens its start selection, so the automated
    // steps (start, editor) are never seen.
    if (net_.hasCharacter()) showLoading("Joining the server");
    rebuilding_ = true;  // until the character is in place (returning) or sent (new)
    mygui::click(newGame);
}

void MultiplayerMenu::driveNewGame(float dt) {
    if (confirmWindow_ > 0) {  // Kenshi may ask to confirm leaving the current world
        confirmWindow_ -= dt;
        if (mygui::Widget* yes = mygui::findBySuffixExcept("_ButtonA", kLoadingPrefix, 4)) {
            log("ui: confirming New Game");
            mygui::click(yes);
            confirmWindow_ = 0;
        }
    }
    if (!newGameAsked_) return;
    newGameTimer_ += dt;
    if (newGameTimer_ < 1.5f) return;
    newGameTimer_ = 0;
    if (mygui::Widget* newGame = mygui::findBySuffix("_NewGameButton", 3)) {
        log("ui: character dead, starting a new character (New Game)");
        newGameAsked_ = false;
        rebuilding_ = true;
        confirmWindow_ = 5.0f;
        mygui::click(newGame);
        return;
    }
    // The pause menu is closed: open it with Escape (hardware scan code, as Kenshi reads input).
    INPUT in[2] = {};
    for (INPUT& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.wScan = 0x01;
        i.ki.dwFlags = KEYEVENTF_SCANCODE;
    }
    in[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    log("ui: opening the pause menu for New Game");
}

void MultiplayerMenu::driveDelete(float dt) {
    if (deleteArmedFor_ > 0) deleteArmedFor_ -= dt;
    if (delete_ == Delete::Off) return;
    auto st = net_.status();
    if (st != ClientNet::Status::Disconnected) deleteStarted_ = true;
    if (st == ClientNet::Status::Rejected || (st == ClientNet::Status::Disconnected && deleteStarted_)) {
        delete_ = Delete::Off;
        error_ = "Could not reach the server: nothing was deleted.";
        return;
    }
    if (st != ClientNet::Status::Connected) return;
    if (delete_ == Delete::Connecting) {
        if (!net_.hasCharacter()) {
            error_ = "You have no character on this server.";
        } else {
            net_.reportDeath();  // the server retires it (kept in its dead_characters table)
            delete_ = Delete::Sent;
            return;
        }
    } else if (net_.hasCharacter()) {
        return;  // waiting for the server's answer
    } else {
        error_ = "Character deleted. Connect to make a new one.";
        log("ui: character deleted on the server");
    }
    delete_ = Delete::Off;
    disconnect_();
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

void MultiplayerMenu::enforceNormalSpeed() {
    // GameWorld + 0x700: game speed (0 paused, 1 normal, 2 and 5 fast), docs/re/game-world.md.
    constexpr uintptr_t kGameSpeed = 0x2134110 + 0x700;
    // GameWorld + 0x8b9: set while a menu (Escape) pauses the world; the per-frame time update
    // multiplies by 0 then (RVA 0x66f190). A multiplayer world never stops.
    constexpr uintptr_t kMenuPaused = 0x2134110 + 0x8b9;
    uint8_t paused = 0;
    if (mem::readBytes(mem::moduleBase() + kMenuPaused, &paused, 1) && paused == 1) {
        uint8_t zero = 0;
        mem::writeBytes(mem::moduleBase() + kMenuPaused, &zero, 1);
        if (!menuPauseLogged_) log("ui: menu pause refused in multiplayer");
        menuPauseLogged_ = true;
    }
    float speed = 1.0f;
    if (!mem::readBytes(mem::moduleBase() + kGameSpeed, &speed, sizeof(speed)) || speed == 1.0f) return;
    // Kenshi's own "normal speed" button keeps its UI (paused banner, button states) consistent.
    if (mygui::Widget* normal = mygui::findBySuffix("_TimeSpeedButton2", 12)) {
        mygui::click(normal);
        log("ui: game speed %.0f refused in multiplayer, back to normal", speed);
    }
}

void MultiplayerMenu::update(float dt) {
    driveNewGame(dt);
    driveDelete(dt);
    if (!mygui::available()) return;
    pollTimer_ += dt;
    if (pollTimer_ >= 0.5f) {
        pollTimer_ = 0;
        ensureButton();
        applyConnectedMenu();
        applyPauseMenu();
        // Players never control a multiplayer world's time (no pause, no speed-up): once the world
        // is really being played (not in the character editor or behind our loading screen).
        timeLocked_ = multiplayerWorld_ && inGame_() && net_.status() == ClientNet::Status::Connected &&
                      !loadingShown_ && !rebuilding_ && !mygui::findShownBySuffix("_ConfirmButton", 12);
        if (serverDay_ && multiplayerWorld_ && net_.status() == ClientNet::Status::Connected)
            if (mygui::Widget* dayText = mygui::findBySuffix("_DayText", 12))
                mygui::setProperty(dayText, "Caption", "Day: " + std::to_string(serverDay_));
        for (const char* suffix : {"_SpeedButtonsPanel", "_SpeedArrowLeft", "_SpeedArrowRight", "_PausedPanel"})
            if (mygui::Widget* w = mygui::findBySuffix(suffix, 12)) mygui::setVisible(w, !timeLocked_);
        if (windowOpen_ && !mygui::findWidget(kWindow)) {  // window closed by the game (e.g. menu rebuilt)
            browser_.stop();
            windowOpen_ = false;
        }
        if (windowOpen_) refreshRows();
    }
    updateLoading(dt);  // every frame: no gap between Kenshi's loading screen and ours
    if (timeLocked_) enforceNormalSpeed();
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
        // New player confirming Kenshi's character editor: export the look first (Kenshi writes
        // <name>.bod2), the client then sends it to the server with the new character.
        if (multiplayerWorld_ && pressedOn_ && !net_.hasCharacter() &&
            mygui::nameOf(static_cast<mygui::Widget*>(pressedOn_)).ends_with("_ConfirmButton")) {
            if (mygui::Widget* exportButton = mygui::findShownBySuffix("_ExportButton", 12)) {
                mygui::click(exportButton);
                log("ui: new character confirmed, look exported");
            }
        }
        if (multiplayerWorld_ && pressedOn_ &&
            mygui::nameOf(static_cast<mygui::Widget*>(pressedOn_)).ends_with("_ExitGameButton")) {
            log("ui: Disconnect asked, waiting for the exit confirmation");
            disconnectPending_ = true;
        } else if (disconnectPending_ && pressedOn_) {
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
