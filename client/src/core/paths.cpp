#include <core/paths.hpp>

namespace wok {

std::string modDirFromExe(std::string_view exePath) {
    size_t slash = exePath.find_last_of("\\/");
    std::string_view dir = slash == std::string_view::npos ? std::string_view{} : exePath.substr(0, slash);
    return std::string(dir) + "\\WorldOfKenshi";
}

std::string modFile(std::string_view modDir, std::string_view fileName) {
    return std::string(modDir) + "\\" + std::string(fileName);
}

}  // namespace wok
