#pragma once
// Automatic updates: at start-up, on its own thread, looks for a newer release on the project's
// GitHub page and installs its client DLL (the running DLL is renamed, the new one takes its
// place and loads at the next start). See core/update.hpp.

#include <string>

namespace wok {

void startAutoUpdate(bool enabled);
// Empty, or a line for the player ("... installed - restart Kenshi").
std::string updateNotice();

}  // namespace wok
