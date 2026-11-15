#pragma once
// A "Multiplayer" tab in Kenshi's own Options window, built exactly like Kenshi builds its tabs:
// TabControl::addItem, ForgottenGUI::createDatapanel, DatapanelGUI lines bound to our variables,
// then the tab's item data set to the panel (an Any holding DatapanelGUI*, made by Kenshi's own
// helper). A tab without that item data makes Kenshi abort ("Bad cast ... DatapanelGUI").

namespace wok {

struct MultiplayerOptions {
    // Chat
    float chatOpacity = 85.0f;  // percent, 20..100: the chat frame's opacity
    float chatFadeSec = 20.0f;  // seconds a message stays (5..120)
    // Players
    bool showNames = true;      // names above the other players
    float nameRange = 500.0f;   // game units (50..2000)
    // Updates
    bool autoUpdate = true;     // install new versions from GitHub at start-up
};

// Called on a poll: adds the tab when Kenshi's Options window is open and does not have it yet.
// The panel writes straight into `options` while the player moves the slider.
void ensureOptionsTab(MultiplayerOptions& options);

}  // namespace wok
