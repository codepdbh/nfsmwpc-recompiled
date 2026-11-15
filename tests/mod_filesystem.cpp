#include "../android/app/src/main/cpp/mod_filesystem.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static void writeFile(const fs::path& path, const char* contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    require(bool(out), "create fixture file");
    out << contents;
    require(bool(out), "write fixture file");
}

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("nfsmw-mod-overlay-" + std::to_string(nonce));
    const fs::path outside = fs::temp_directory_path() / ("nfsmw-mod-overlay-outside-" + std::to_string(nonce));
    std::error_code cleanupError;

    try {
        fs::create_directories(root / "CARS");
        fs::create_directories(outside);
        writeFile(root / "CARS" / "stock.bin", "original");
        writeFile(root / "modloader" / "Alpha" / "cars" / "STOCK.BIN", "alpha");
        writeFile(root / "modloader" / "Zulu" / "CARS" / "Stock.bin", "zulu");
        writeFile(root / "modloader" / ".Disabled" / "CARS" / "stock.bin", "disabled");
        writeFile(root / "modloader" / "Zulu" / "CARS" / "new_asset.bin", "new asset");
        writeFile(root / "modloader" / "Zulu" / "CARS" / "NewFolder" / "inside.bin", "inside");
        writeFile(outside / "escape.bin", "outside");

        std::error_code symlinkError;
        fs::create_directory_symlink(outside, root / "modloader" / "Zulu" / "CARS" / "escape", symlinkError);

        fs::path replacement;
        std::string modName;
        require(nfs_mod_fs::resolveFile(root, fs::path("cars/stock.BIN"), replacement, modName),
                "find a case-insensitive override");
        require(modName == "Zulu", "alphabetically last enabled mod wins");
        require(replacement.filename() == "Stock.bin", "return the winning mod's actual filename");

        require(!nfs_mod_fs::resolveFile(root, fs::path("CARS/base_only.bin"), replacement, modName),
                "missing overlay falls back to the original game tree");
        require(!nfs_mod_fs::resolveFile(root, fs::path("modloader/Zulu/CARS/Stock.bin"), replacement, modName),
                "the loader's own folder cannot be overridden");
        require(!nfs_mod_fs::resolveFile(root, fs::path("_outside/probe.bin"), replacement, modName),
                "the outside-path sandbox cannot be overridden");
        require(!nfs_mod_fs::resolveFile(root, fs::path("../outside.bin"), replacement, modName),
                "parent traversal is rejected");

        const auto overlays = nfs_mod_fs::overlayDirectories(root, fs::path("cars"));
        require(overlays.size() == 2, "enabled mod directories are available for virtual directory merging");
        require(nfs_mod_fs::overlayDirectories(root, fs::path("." )).empty(),
                "mod roots are not exposed as the game root");
        require(nfs_mod_fs::overlayDirectories(root, fs::path("modloader")).empty(),
                "mod storage is not recursively exposed");

        require(nfs_mod_fs::resolveDirectory(root, fs::path("cars/newfolder"), replacement, modName),
                "find a directory that exists only in a mod");
        require(modName == "Zulu" && fs::is_directory(replacement),
                "directory metadata uses the same priority order");
        require(nfs_mod_fs::resolveAny(root, fs::path("cars/newfolder"), replacement, modName),
                "metadata resolver accepts mod-only directories");

        if (!symlinkError) {
            require(!nfs_mod_fs::resolveFile(root, fs::path("CARS/escape/escape.bin"), replacement, modName),
                    "symlinks cannot escape a mod directory");
        }

        std::cout << "Mod filesystem passed: priority, case-insensitive lookup, disabled mods, "
                     "virtual directories, fallback and path containment\n";
    } catch (...) {
        fs::remove_all(root, cleanupError);
        fs::remove_all(outside, cleanupError);
        throw;
    }

    fs::remove_all(root, cleanupError);
    fs::remove_all(outside, cleanupError);
    return 0;
}
