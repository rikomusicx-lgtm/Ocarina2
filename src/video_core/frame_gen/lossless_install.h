// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace VideoCore::FrameGen {

// Status of an attempt to make the user's own Lossless Scaling install usable. Nothing here
// downloads anything: it finds the Lossless Scaling the user already owns and copies its
// Lossless.dll into the ocarina2 profile, which is all the emulator ever needs.
enum class LosslessInstallStatus {
    AlreadyPresent, // The profile already ships a valid Lossless.dll.
    Installed,      // Found a Steam install and copied its Lossless.dll into the profile.
    NotFound,       // No Lossless Scaling install was located on this machine.
    CopyFailed,     // Found, but the copy failed (permissions, disk space, ...).
    InvalidDll,     // Found but failed validation; was not installed.
};

struct LosslessInstallResult {
    LosslessInstallStatus status{LosslessInstallStatus::NotFound};
    std::string source;  ///< Where the DLL came from (empty when not found).
    std::string message; ///< User-facing explanation for the GUI.
};

/// Looks for Lossless Scaling in the Steam libraries of this machine, copies its
/// Lossless.dll into the ocarina2 lossless folder and validates the result.
/// Safe to call repeatedly; does nothing when the profile already has a valid DLL.
LosslessInstallResult TryAutoInstallLosslessDll();

} // namespace VideoCore::FrameGen
