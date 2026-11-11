// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// yuzu data directories

// Ocarina2 keeps its own profile: %APPDATA%\ocarina2 on Windows (and the ocarina2 directories
// under $XDG_DATA_HOME, $XDG_CONFIG_HOME and $XDG_CACHE_HOME on Linux and macOS). v1 renamed it
// from fritchii, which is still picked up as a migration source for players coming from the
// v5.8 profile builds. eden and lichii remain sources as before.
#define EDEN_DIR "ocarina2"
#define EDEN_LEGACY_DIR "eden"
#define FRITCHII_DIR "fritchii"
#define LICHII_DIR "lichii"
#define PORTABLE_DIR "user"

// Sub-directories contained within a yuzu data directory
#define AMIIBO_DIR "amiibo"
#define CACHE_DIR "cache"
#define CONFIG_DIR "config"
#define CRASH_DUMPS_DIR "crash_dumps"
#define DUMP_DIR "dump"
#define KEYS_DIR "keys"
#define LOAD_DIR "load"
#define LOG_DIR "log"
#define LOSSLESS_DIR "lossless"
#define NAND_DIR "nand"
#define PLAY_TIME_DIR "play_time"
#define POST_PRESET_DIR "post_presets"
#define POST_SHADER_DIR "post_shaders"
#define SCREENSHOTS_DIR "screenshots"
#define SDMC_DIR "sdmc"
#define SHADER_DIR "shader"
#define STREAMLINE_DIR "streamline"
#define FIDELITYFX_DIR "fidelityfx"
#define TAS_DIR "tas"
#define ICONS_DIR "icons"

// Compatibility with other emulators
#define CITRON_DIR "citron"
#define SUDACHI_DIR "sudachi"
#define YUZU_DIR "yuzu"
#define SUYU_DIR "suyu"
#define RYUJINX_DIR "Ryujinx"

// yuzu-specific files
#define LOG_FILE "eden_log.txt"
#define LOSSLESS_DLL_FILE "Lossless.dll"
#define LOSSLESS_CACHE_FILE "lsfg_spirv.cache"
