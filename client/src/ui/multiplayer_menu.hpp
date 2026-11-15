#pragma once
// "Multiplayer" button on Kenshi's title screen + a server browser window: one row per server
// (version, name, description, players, ping), selection, Connect / Unlink / Close.

#include "../net_client.hpp"
#include "../master_fetch.hpp"
#include "../server_browser.hpp"
#include "../bug_report.hpp"

#include <core/config.hpp>

#include <functional>
#include <string>

namespace wok {

class MultiplayerMenu {
public:
    // `saveConfig` persists the last used server; `connect` starts the connection to cfg.host/port.
    // `unlink` forgets this PC's server tokens (next connection asks Steam again).
    // `inGame` tells whether a world is loaded (the pause menu reuses the main menu layout).
    MultiplayerMenu(ClientConfig& cfg, ClientNet& net, std::function<void()> saveConfig, std::function<void()> connect,
                    std::function<void()> unlink, std::function<bool()> inGame, std::function<void()> disconnect);
    void update(float dt);  // game thread, every frame

private:
    void ensureButton();
    void openWindow();
    void closeWindow();
    void onClick(const std::string& widgetName);
    void refreshStatus();
    void refreshRows();
    void select(int row);
    void applyConnectedMenu();  // connected on the title screen: start the server game
    void applyPauseMenu();      // connected: no local save/load in the pause menu, Disconnect instead
    void enforceNormalSpeed();  // multiplayer: the game always runs at normal speed
    bool timeLocked_ = false;
    bool timeUiHidden_ = false;  // we hid Kenshi's speed controls
    bool devNoTimeLock_ = false;  // dev switch (file WorldOfKenshi/dev_no_timelock)
    bool menuPauseLogged_ = false;

    ClientConfig& cfg_;
    ClientNet& net_;
    std::function<void()> saveConfig_;
    std::function<void()> connect_;
    std::function<void()> unlink_;
    std::function<bool()> inGame_;
    std::function<void()> disconnect_;
    ServerBrowser browser_;
    MasterFetch master_;
    BugReporter reporter_;
    void openReportWindow();
    void sendReport();
    std::vector<ServerEntry> listed_;  // rows of the open window: wok_client.ini servers + master list
    int selected_ = -1;
    size_t rowCount_ = 0;
    float pollTimer_ = 0;
    float statusTimer_ = 0;
    bool mouseWasDown_ = false;
    void* pressedOn_ = nullptr;
    bool windowOpen_ = false;
    std::string lastStatus_;
    std::string error_;
    bool enteredGame_ = false;
    bool multiplayerWorld_ = false;   // the loaded world was started from multiplayer (until the title screen)
    int autoStep_ = 0;                // returning player: 1 = Begin clicked, 2 = editor confirmed
    int confirmWaits_ = 0;
    int nameTries_ = 0;    // editor name typing attempts this connection
    int importStage_ = 0;  // returning player's look: 0 to do, 1 file dialog open, 2 done
    std::string importFile_;
    int importTries_ = 0;
    int genderClicks_ = 0;
    int listTries_ = 0;
    bool exitRenamed_ = false;        // pause menu's Exit Game currently reads "Disconnect"
    bool disconnectPending_ = false;  // Disconnect clicked, waiting for Kenshi's exit dialog answer
    // Loading screen covering the automated steps of a returning player until the world is ready.
    bool loadingShown_ = false;
    bool worldReady_ = false;
    float loadingTime_ = 0;
    std::string loadingText_;
    const char* lastTip_ = nullptr;
    int lastSlide_ = -1;
    int lastTipSlot_ = -1;
    size_t lastImage_ = 0;
    void updateLoading(float dt);
    void showLoading(const std::string& text);
    void hideLoading();

public:
    // Called by the client once the returning player's character is back in place.
    void setWorldReady() {
        worldReady_ = true;
        rebuilding_ = false;
        settleTimer_ = kSettleSec;
    }
    // Permanent death: run Kenshi's New Game from the pause menu, staying connected.
    void restartForNewCharacter() { newGameAsked_ = true; newGameTimer_ = 0; }
    // Permanent death: asks the player "New character" (restartForNewCharacter) or "Exit".
    void showDeathWindow();
    // The new character reached the server: the rebuilt world is played again (time lock back on).
    void newCharacterSent() {
        rebuilding_ = false;
        settleTimer_ = kSettleSec;
    }

private:
    bool newGameAsked_ = false;
    // "Delete character": armed by a first click, then connect, retire the character, disconnect.
    float deleteArmedFor_ = 0;  // seconds left to confirm with a second click
    enum class Delete { Off, Connecting, Sent } delete_ = Delete::Off;
    bool deleteStarted_ = false;  // the connection attempt has begun
    void driveDelete(float dt);
public:
    bool deletingCharacter() const { return delete_ != Delete::Off; }
    // The server's day, shown in the time panel instead of Kenshi's local day counter (which is
    // left alone: moving it ages every character, see world_time.cpp). 0 = not in a server world.
    void setServerDay(uint32_t day) { serverDay_ = day; }
    void setDevNoTimeLock(bool v) { devNoTimeLock_ = v; }
private:
    uint32_t serverDay_ = 0;
    std::string dayTextName_;  // full name of Kenshi's "Day: N" label, found once
public:
private:
    // A world that just arrived keeps Kenshi's start pause this long before the time lock acts:
    // unpausing while Kenshi is still starting its sound broke the audio.
    static constexpr float kSettleSec = 5.0f;
    float settleTimer_ = 0;
    bool rebuilding_ = false;  // Kenshi's New Game is building the world: let it pause (time lock off)
    float newGameTimer_ = 0;  // seconds since the last attempt
    float confirmWindow_ = 0; // seconds left to accept Kenshi's "lose progress?" dialog
    void driveNewGame(float dt);
};

}  // namespace wok
