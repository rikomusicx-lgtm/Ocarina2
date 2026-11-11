// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/fs/fs_paths.h"
#include "common/logging.h"
#include "video_core/frame_gen/lossless_dll.h"
#include "video_core/frame_gen/lossless_install.h"

namespace VideoCore::FrameGen {

namespace {

constexpr char LOSSLESS_SCALING_COMMON[] = "Lossless Scaling";
#if defined(_WIN32)
constexpr char LOSSLESS_DLL_RELATIVE[] = "common\\Lossless Scaling\\Lossless.dll";
constexpr char STEAMAPPS_DIR[] = "steamapps";
#else
constexpr char LOSSLESS_DLL_RELATIVE[] = "common/Lossless Scaling/Lossless.dll";
constexpr char STEAMAPPS_DIR[] = "steamapps";
#endif

[[nodiscard]] std::vector<std::filesystem::path> LibraryFoldersFromVdf(const std::filesystem::path& vdf) {
    std::vector<std::filesystem::path> folders;
    std::ifstream file{vdf};
    if (!file) {
        return folders;
    }

    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    size_t pos = 0;
    while ((pos = text.find("\"path\"", pos)) != std::string::npos) {
        const size_t first_quote = text.find('"', pos + 6);
        if (first_quote == std::string::npos) {
            break;
        }
        const size_t second_quote = text.find('"', first_quote + 1);
        if (second_quote == std::string::npos) {
            break;
        }
        std::string value = text.substr(first_quote + 1, second_quote - first_quote - 1);
        // Steam escapes backslashes in libraryfolders.vdf with double backslashes.
        for (size_t hit = 0; (hit = value.find("\\\\", hit)) != std::string::npos;) {
            value.replace(hit, 2, "\\");
            hit += 1;
        }
        folders.emplace_back(value);
        pos = second_quote;
    }
    return folders;
}

std::vector<std::filesystem::path> SteamRoots() {
    std::vector<std::filesystem::path> roots;
    const auto add = [&roots](const std::filesystem::path& root) {
        if (!root.empty()) {
            roots.push_back(root);
        }
    };

#if defined(_WIN32)
    // The running setup's install location is recorded under HKCU; HKLM covers admin installs.
    for (const HKEY hkey : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        DWORD type{};
        std::wstring value;
        value.resize(512);
        DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        const LSTATUS status = RegGetValueW(hkey, L"SOFTWARE\\Valve\\Steam", L"SteamPath",
                                            RRF_RT_ANY, &type, value.data(), &size);
        if (status == ERROR_SUCCESS) {
            value.resize(size / sizeof(wchar_t));
            while (!value.empty() && value.back() == L'\0') {
                value.pop_back();
            }
            add(std::filesystem::path{value});
        }
    }
    add(L"C:\\Program Files (x86)\\Steam");
    add(L"C:\\Program Files\\Steam");
    add(L"C:\\Steam");
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        add(std::filesystem::path{home} / "Library" / "Application Support" / "Steam");
    }
#else
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path h{home};
        add(h / ".steam" / "steam");
        add(h / ".local" / "share" / "Steam");
        add(h / ".steam" / "debian-installation");
        add(h / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
    }
    if (const char* steam_home = std::getenv("STEAM_HOME")) {
        add(std::filesystem::path{steam_home});
    }
#endif

    // Non-default library folders registered by Steam (e.g. another drive).
    const size_t initial = roots.size();
    for (size_t i = 0; i < initial; ++i) {
        for (const auto& folder : LibraryFoldersFromVdf(roots[i] / STEAMAPPS_DIR / "libraryfolders.vdf")) {
            add(std::move(folder));
        }
    }

    std::ranges::sort(roots);
    const auto last = std::ranges::unique(roots).begin();
    roots.erase(last, roots.end());
    return roots;
}

[[nodiscard]] std::filesystem::path FindLosslessDll() {
    for (const auto& root : SteamRoots()) {
        const std::filesystem::path candidate = root / STEAMAPPS_DIR / LOSSLESS_DLL_RELATIVE;
        if (Common::FS::Exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // Anonymous namespace

LosslessInstallResult TryAutoInstallLosslessDll() {
    const std::filesystem::path installed = GetLosslessDllPath();
    if (ValidateLosslessDll(installed) == LosslessStatus::Ok) {
        return {.status = LosslessInstallStatus::AlreadyPresent,
                .source = installed.string(),
                .message = "Lossless.dll is already installed and valid."};
    }

    const std::filesystem::path source = FindLosslessDll();
    if (source.empty()) {
        const LosslessInstallResult result{
            .status = LosslessInstallStatus::NotFound,
            .source{},
            .message = "No Lossless Scaling install was found on this machine. LSFG needs "
                       "Lossless Scaling (Steam); copy its Lossless.dll into the 'lossless' folder "
                       "of the user directory. Optical Flow FG works without any download."};
        LOG_WARNING(Render_Vulkan, "LSFG: {}", result.message);
        return result;
    }

    const std::filesystem::path destination = installed;
    if (!Common::FS::CreateDirs(destination.parent_path())) {
        return {.status = LosslessInstallStatus::CopyFailed,
                .source = source.string(),
                .message = "Found Lossless Scaling at " + source.string() +
                           " but could not create the ocarina2 lossless folder."};
    }

    std::error_code copy_error;
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        return {.status = LosslessInstallStatus::CopyFailed,
                .source = source.string(),
                .message = "Found Lossless Scaling at " + source.string() +
                           " but could not copy Lossless.dll: " + copy_error.message()};
    }

    if (ValidateLosslessDll(destination) != LosslessStatus::Ok) {
        void(Common::FS::RemoveFile(destination));
        return {.status = LosslessInstallStatus::InvalidDll,
                .source = source.string(),
                .message = "The Lossless.dll at " + source.string() +
                           " failed validation and was not installed."};
    }

    LOG_INFO(Render_Vulkan, "LSFG: installed Lossless.dll from {}", source.string());
    return {.status = LosslessInstallStatus::Installed,
            .source = source.string(),
            .message = "Lossless.dll was copied from " + source.string() + " and validated."};
}

} // namespace VideoCore::FrameGen
