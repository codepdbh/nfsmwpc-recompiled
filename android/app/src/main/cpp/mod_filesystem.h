#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// Read-only, mirrored-folder overrides for the Android PC recompile.
// Mod folders are scanned once per game process; the Mods Manager must finish
// changes before launching/relaunching the game.
namespace nfs_mod_fs {

inline std::string lowerName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool safeRelative(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return false;
    for (const auto& part : relative)
        if (part == "..") return false;
    return true;
}

inline bool within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code rootError, candidateError;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, rootError);
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, candidateError);
    if (rootError || candidateError) return false;
    const auto relative = canonicalCandidate.lexically_relative(canonicalRoot);
    if (relative.empty()) return canonicalCandidate == canonicalRoot;
    for (const auto& part : relative)
        if (part == "..") return false;
    return true;
}

// Resolve each component without depending on Android's case-sensitive host
// filesystem: the guest Windows filesystem is case-insensitive.
inline std::filesystem::path caseInsensitivePath(
        const std::filesystem::path& base, const std::filesystem::path& relative) {
    auto current = base;
    for (const auto& part : relative) {
        if (part == "." || part.empty()) continue;
        if (part == "..") return {};
        bool found = false;
        std::error_code directoryError;
        if (std::filesystem::is_directory(current, directoryError) && !directoryError) {
            std::filesystem::directory_iterator it(current, directoryError), end;
            while (!directoryError && it != end) {
                if (lowerName(it->path().filename().string()) == lowerName(part.string())) {
                    current = it->path();
                    found = true;
                    break;
                }
                it.increment(directoryError);
            }
        }
        if (!found) current /= part;
    }
    return current;
}

inline const std::vector<std::filesystem::path>& enabledModDirectories(
        const std::filesystem::path& gameRoot) {
    static std::filesystem::path cachedRoot;
    static std::vector<std::filesystem::path> cached;
    static bool loaded = false;

    std::error_code rootError;
    auto canonicalRoot = std::filesystem::weakly_canonical(gameRoot, rootError);
    if (rootError) canonicalRoot = gameRoot.lexically_normal();
    if (loaded && cachedRoot == canonicalRoot) return cached;

    loaded = true;
    cachedRoot = canonicalRoot;
    cached.clear();

    const auto modRoot = canonicalRoot / "modloader";
    std::error_code error;
    if (!within(canonicalRoot, modRoot) || !std::filesystem::is_directory(modRoot, error) || error)
        return cached;

    std::filesystem::directory_iterator it(modRoot, error), end;
    while (!error && it != end) {
        const auto path = it->path();
        const auto name = path.filename().string();
        std::error_code statusError;
        const auto status = it->symlink_status(statusError);
        // A leading dot disables a mod. Only real directories are accepted;
        // symlinked mod roots would make the overlay escape its data folder.
        if (!statusError && !name.empty() && name.front() != '.' &&
                std::filesystem::is_directory(status) && within(modRoot, path))
            cached.push_back(path);
        it.increment(error);
    }

    std::sort(cached.begin(), cached.end(), [](const auto& a, const auto& b) {
        const auto an = lowerName(a.filename().string());
        const auto bn = lowerName(b.filename().string());
        return an == bn ? a.filename().string() < b.filename().string() : an < bn;
    });
    return cached;
}

inline bool resolveFile(const std::filesystem::path& gameRoot,
        const std::filesystem::path& relative, std::filesystem::path& replacement,
        std::string& modName) {
    if (!safeRelative(relative)) return false;
    auto first = relative.begin();
    if (first != relative.end()) {
        const auto top = lowerName(first->string());
        // Do not allow the game to recursively override the loader itself or
        // files which the Android bridge maps outside the game installation.
        if (top == "modloader" || top == "_outside") return false;
    }

    bool found = false;
    for (const auto& mod : enabledModDirectories(gameRoot)) {
        const auto candidate = caseInsensitivePath(mod, relative);
        std::error_code error;
        if (candidate.empty() || !std::filesystem::is_regular_file(candidate, error) || error ||
                !within(mod, candidate))
            continue;
        replacement = candidate;
        modName = mod.filename().string();
        found = true; // Later (alphabetically higher) enabled mods win.
    }
    return found;
}

inline std::vector<std::filesystem::path> overlayDirectories(
        const std::filesystem::path& gameRoot, const std::filesystem::path& relative) {
    std::vector<std::filesystem::path> result;
    if (!safeRelative(relative) || relative == "." || relative == "modloader" || relative == "_outside")
        return result;
    for (const auto& mod : enabledModDirectories(gameRoot)) {
        const auto candidate = caseInsensitivePath(mod, relative);
        std::error_code error;
        if (!candidate.empty() && std::filesystem::is_directory(candidate, error) && !error &&
                within(mod, candidate))
            result.push_back(candidate);
    }
    return result;
}

} // namespace nfs_mod_fs
