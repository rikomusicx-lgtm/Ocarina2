// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include "common/memory_detect.h"
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
    // The preset mirrors the owner's tuned handheld Ocarina of Time session: Fast GPU mode
    // with immediate fences/VSync and unsafe DMA reads, 8 GB guest layout, aggressive VRAM
    // usage and Force Max Clock.
    session(settings.resolution_setup, ResolutionSetup::Res1X);
    session(settings.gpu_accuracy, GpuAccuracy::Low);
    session(settings.use_asynchronous_gpu_emulation, true);
    session(settings.use_asynchronous_shaders, true);
    session(settings.astc_recompression, AstcRecompression::Bc3);
    session(settings.accelerate_astc, AstcDecodeMode::Gpu);
    session(settings.vram_usage_mode, VramUsageMode::Aggressive);
    session(settings.nvdec_emulation, NvdecEmulation::Gpu);
    session(settings.use_reactive_flushing, false);
    session(settings.vsync_mode, VSyncMode::Immediate);
    session(settings.gpu_fence_behavior, GpuFenceBehavior::Immediate);
    session(settings.dma_accuracy, DmaAccuracy::Unsafe);
    session(settings.async_presentation, true);
    session(settings.renderer_force_max_clock, true);
    session(settings.frame_gen, true);
    session(settings.frame_gen_multiplier, 3);
    session(settings.scaling_filter, ScalingFilter::Area);
    session(settings.anti_aliasing, AntiAliasing::None);
    session(settings.post_shader_enabled, false);
    session(settings.nvidia_dlss, false);
    session(settings.max_anisotropy, AnisotropyMode::Automatic);
    session(settings.frame_pacing_mode, FramePacingMode::Target_Auto);
    session(settings.aspect_ratio, AspectRatio::R16_9);
    session(settings.gpu_unswizzle_texture_size, GpuUnswizzleSize::VeryLarge);
    // 12 GB emulation removes Kokiri's streaming stalls (the fork's 25 FPS case). It needs the
    // host to back the mapping, so only enable it on machines with the memory to pay for it;
    // lower-end PCs (8-12 GB RAM) keep the game's 8 GB floor.
    const u64 host_ram = Common::GetMemInfo().TotalPhysicalMemory;
    const auto layout = host_ram >= (u64{16} << 30) ? MemoryLayout::Memory_12Gb
                                                   : MemoryLayout::Memory_8Gb;
    session(settings.memory_layout_mode, layout);
    session(settings.cpu_accuracy, CpuAccuracy::Unsafe);
    session(settings.sync_core_speed, true);
    session(settings.use_docked_mode, ConsoleMode::Handheld);
    // Max Resolution is on by default: it fixes the render at the full scale for the console
    // mode, which is what users on a 3050-class card want. Turning it off only lowers internal
    // resolution (about 2-3 fps).
    // The GPU thread split is turned off for this preset: the mesh-shader workload produces
    // zero-indirect-draw frames when the indirect buffer is read before the GPU writer commits
    // it (the page-map race) - empirically, water and the scene go black in bursts with the
    // split on, and the flashes stop with it off.
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
