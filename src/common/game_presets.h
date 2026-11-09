// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <string_view>
#include "common/settings.h"

namespace Settings {

inline bool EnvOotPresetEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_OOT_PRESET");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

inline constexpr u64 MarioKartWorldProgramId = 0x0400C3F00006E000ULL;
inline constexpr u64 OcarinaOfTimeProgramId = 0x0400486024DEC000ULL;

inline void SetMarioKartWorldRecommendedSettings(Values& settings, bool global) {
    const auto session = [global](auto& setting, auto value) {
        setting.SetGlobal(global);
        setting.SetValue(value);
    };
    session(settings.renderer_backend, RendererBackend::Vulkan);
    session(settings.resolution_setup, ResolutionSetup::Res1X);
    session(settings.gpu_accuracy, GpuAccuracy::Low);
    session(settings.use_asynchronous_shaders, true);
    session(settings.astc_recompression, AstcRecompression::Bc3);
    session(settings.vram_usage_mode, VramUsageMode::Conservative);
    session(settings.use_reactive_flushing, false);
    session(settings.vsync_mode, VSyncMode::FifoRelaxed);
    session(settings.use_docked_mode, ConsoleMode::Handheld);
    session(settings.scaling_filter, ScalingFilter::ScaleForce);
    session(settings.anti_aliasing, AntiAliasing::Fxaa);
}

inline bool ApplyMarioKartWorldPreset(Values& settings, u64 program_id, bool accepted) {
    if (!accepted || program_id != MarioKartWorldProgramId) {
        return false;
    }
    SetMarioKartWorldRecommendedSettings(settings, false);
    return true;
}

inline void SetOcarinaOfTimePerformanceSettings(Values& settings, bool global,
                                               bool max_resolution) {
    const auto session = [global](auto& setting, auto value) {
        setting.SetGlobal(global);
        setting.SetValue(value);
    };
    session(settings.renderer_backend, RendererBackend::Vulkan);
    // 1080p docked: render at 0.75x (1080p native presentation), which still leaves the game
    // at its native docked resolution. The 1440p internal path the remake asks for exceeds
    // what a 6 GB card can drive at 30 FPS in this scene, and NativeSeqRef at 1080p is the
    // baseline this fork exists for on low-end PCs.
    // EDEN_NX2_OOT_RESOLUTION=1X overrides the downscale: at 0.75x the emulator rescales guest
    // images (for the host) and the depth view stops matching the DLSS input contract, so DLSS
    // can never engage; 1X renders natively and lets DLSS replace the game's upscaler.
    const char* const resolution_override = std::getenv("EDEN_NX2_OOT_RESOLUTION");
    const ResolutionSetup resolution =
        (resolution_override && std::string_view{resolution_override} == "1X")
            ? ResolutionSetup::Res1X
            : ResolutionSetup::Res3_4X;
    session(settings.resolution_setup, resolution);
    session(settings.gpu_accuracy, GpuAccuracy::Low);
    session(settings.use_asynchronous_shaders, true);
    session(settings.astc_recompression, AstcRecompression::Bc3);
    session(settings.vram_usage_mode, VramUsageMode::Conservative);
    session(settings.nvdec_emulation, NvdecEmulation::Gpu);
    session(settings.use_reactive_flushing, false);
    // Don't override the user's Docked/Handheld choice: the built-in Max Resolution patch keeps
    // the render at 1080p either way, and the console mode only changes the guest's output size.
    session(settings.scaling_filter, ScalingFilter::Bilinear);
    session(settings.anti_aliasing, AntiAliasing::None);
    session(settings.post_shader_enabled, false);
    // Max Resolution is on by default: it fixes the render at 1080p, which is what users on a
    // 3050-class card want. Turning it off only lowers internal resolution (about 2-3 fps).
    // The GPU thread split is turned off for this preset: at docked the mesh-shader workload
    // produces zero-indirect-draw frames when the indirect buffer is read before the GPU writer
    // commits it (the page-map race) - empirically, water and the scene go black in bursts with
    // the split on, and the flashes stop with it off.
    settings.oot_split_disabled = true;
    settings.oot_max_resolution_preset_enabled = max_resolution;
    settings.oot_dynamic_fps_preset_enabled = true;
    settings.oot_preflush_preset_enabled = true;
}

inline bool ApplyOcarinaOfTimePerformancePreset(Values& settings, u64 program_id, bool accepted,
                                               bool max_resolution) {
    if (program_id != OcarinaOfTimeProgramId) {
        return false;
    }
    if (!accepted) {
        settings.oot_max_resolution_preset_enabled = false;
        settings.oot_dynamic_fps_preset_enabled = false;
        settings.oot_preflush_preset_enabled = false;
        settings.oot_split_disabled = false;
        return false;
    }
    SetOcarinaOfTimePerformanceSettings(settings, false, max_resolution);
    return true;
}

} // namespace Settings
