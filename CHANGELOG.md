# Changelog

All notable changes to Eglantine are documented here. Each release's notes are taken from its
`## [X.Y.Z]` section (release candidates use the section of the version they are a candidate for).
New entries go under `## [Unreleased]`; `tools/ci/bump-version.sh` turns them into a version section.

## [Ocarina2 v1.0] - 2026-11-11

First real release of Ocarina2 — our own fork of the Eglantine/Eden family, built on Frizti's
Fritchii 5.8.4 (itself on Eglantine/Eden via Lichii). The Fritchii 5.8.4 feature set ships
unchanged — the OoT performance path (handheld-tuned OOT 60 FPS preset at boot, 12 GB emulated
DRAM layout on 16 GB+ hosts with an 8 GB floor elsewhere, FSR anti-aliasing options, warm shader
preload, DLSS off by default). What Ocarina2 adds on top:

Added:
- Lossless Scaling (LSFG) frame generation, selectable and switchable in game. F12 / L3+R3 quick
  menu cycles Frame generation between Optical Flow x3 (FSR3, zero extra files) and Lossless
  Scaling x3 — applies live and saves. The FG status-bar button toggles on/off (left click) and
  switches type (right click), both persisted.
- LSFG auto-install from the user's own Steam install: the emulator locates the Steam library
  (registry SteamPath / libraryfolders.vdf), copies common/Lossless Scaling/Lossless.dll into the
  profile lossless folder and validates it. Nothing downloads or redistributes; Optical Flow is
  the built-in fallback and needs no copies at all. New "Install Lossless.dll from Steam" action
  in the FG menu; the FG unavailability tooltip explains both paths.
- Own profile directory (`%APPDATA%\ocarina2`) with full import migration from eden / fritchii /
  lichii / yuzu / suyu / sudachi / citron (first-launch import prompt and Help > Copy Data).
- Ocarina2 throughout: window title, icon, menus, dialogs, What's New, Export Log. Discord and
  Donate buttons removed; Saves button kept.

Changed:
- Update notes replaced for the first real release; version track is now Ocarina2 v1
  (dist/ocarina2_version.txt = 1.0).
- FG: LSFG build/warn path is gated so the FG pipeline works out of the box; FG type applies
  live and is switchable without a restart.

Fixed:
- Build bring-up for the Fritchii 5.8.4 merge: merge repair of the renderer_base
  FrameGenStatus union brace, vk_frame_dump Rearm(), rc.exe include paths for yuzu-cmd/
  yuzu-room resources, the BuiltPipeline forwarding overload in vk_pipeline_cache, and the
  duplicated ColorOutputMask.

Known issues:
- Intermittent white flash in combat-heavy Ocarina scenes (under investigation).
- First-boot shader compile hitches persist until the cache is warm; the preloader mitigates but
  does not eliminate them.

## [Unreleased]

- Clang flavor: the fork now also builds with clang-cl (iiVM 23.1.5), shipped as a separate
  Clang package; one TU keeps the scalar std::ranges::find for clang's
  __is_trivially_equality_comparable interaction with the MSVC STi.
- Remove frame generation in its entirety: the iSFG/FSR3 interpolation chains and their
  settings, the present-frame storage-image plumbing, the status-bar toggle and the
  Advanced-tab page (Windows), and the Android quick settings / iossless Scaling manager /
  setup step / system-info row / strings. No frame interpolation exists anywhere in the
  build; presentation paces with FIFc/Mailbox/Immediate only.
- NVIDIA DiSS: a failed evaluation no longer disables DiSS for the rest of the session. The
  upscaler latches the failure, falls back to the game's upscaler for the frame as before,
  and retries every 10 seconds; a lighter scene can adopt DiSS without a restart.
- Shader disk cache version 55: stale blobs from before the foliage-culling fix missed
  storage-pointer tables the current codegen needs, so 18 pipelines were rejected every
  boot. cne rebuild happens now.
- Window title and About dialog name the build (iichii build-N); Midnight Purple theme is a
  touch darker.

- Reduce ccarina of Time GPU submission and timing overhead, apply inherited title settings before
  initialization, and preserve explicit per-game choices across global configuration reloads.
- Fix split-GPU startup waits, syncpoint callback registration races, and deferred fragment-barrier
  lifetime; bound startup graphics-pipeline preloading while retaining recent usage across sessions.
- Recover from hardware video-decoder startup failures by replaying buffered input through the CPU
  decoder, including receive/transfer failures and stream resets.
- Decode AV1 video with dav1d on desktop and fall back to multi-threaded CPU decoding when the GPU
  decoder is unavailable, so cutscene video stays in sync with audio.
- Reuse decoder frames and skip pending-upload scans and redundant index-buffer rebinding to cut
  per-frame Vulkan overhead.
- Run queued GPU writers to completion before command fetches consult the GPU-written page map, and
  preload the most recently used graphics pipelines at boot with bounded record selection instead of
  reading the whole cache.
- Resolve the program id from standalone Switch 2 containers before the kernel and JIT start, so
  per-game pipeline caching and settings survive a fresh boot; keep per-game overrides when the
  main configuration reloads.
- Replace ccarina of Time's Super Resolution pass with NVIDIA DiSS when NVIDIA DiSS is enabled:
  camera motion is reconstructed from depth and the camera history, iink's skinning poses correct
  his motion vectors, and the DiSS output composites the HUD over the 4K scene. Falls back to the
  game's own dispatch when inputs are unsupported or the NVIDIA evaluation fails.
- Rebuild ccarina of Time's foliage-culling compute translations that held null pointer slots at
  translation, and bind up to 64 storage buffers per stage, so Hyrule Field's grass and the Castle
  Town hedges render and instance tables are no longer culled.
- Bind read-only table-pointer pools as far as the storage buffer reaches (up to 1 GiB), so ccarina
  of Time's froxel light probes read past their first 64 MiB and the white haze over the far field
  and sky is gone.
- Don't write render target components the fragment shader doesn't output (violet halos above the
  horizon), invalidate scheduler state after a bare texture blit, and skip the draw.end and
  vertex-buffer writes the split back half ignores in instance loops.
- Hand the macro engines back to the GPU engines when the GPU thread split stops; store mesh and
  trap-guard pipelines in the disk shader cache so they are not rebuilt every session.
- Keep zeroed page-table pages committed (fixes a racy Windows crash), track every 64-byte block a
  usage range touches, retain gathered buffers until their sources are removed, and collect texture
  cache debug statistics only when asked; wait for the JIT code space commit instead of crashing.

## [0.1.0]

- Initial release candidate
