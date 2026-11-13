#pragma once
// Where the client keeps its files: "<folder of kenshi_x64.exe>\WorldOfKenshi".

#include <string>
#include <string_view>

namespace wok {

std::string modDirFromExe(std::string_view exePath);
std::string modFile(std::string_view modDir, std::string_view fileName);

}  // namespace wok
