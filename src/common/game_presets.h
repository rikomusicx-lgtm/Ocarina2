// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings.h"

namespace Settings {

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
    session(settings.resolution_setup, ResolutionSetup::Res1X);
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
    // The game is GPU-bound at ~30 fps at 1080p on this class of hardware; the menus already
    // run at 60. Presentation is doubled through the built-in optical-flow interpolator, capped
    // at exactly 60 Hz, instead of lowering the internal resolution.
    session(settings.frame_gen, true);
    session(settings.frame_gen_target_rate, 60);
    // Max Resolution is on by default: it fixes the render at 1080p, which is what users on a
    // 3050-class card want. Turning it off only lowers internal resolution (about 2-3 fps).
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
        return false;
    }
    SetOcarinaOfTimePerformanceSettings(settings, false, max_resolution);
    return true;
}

} // namespace Settings
