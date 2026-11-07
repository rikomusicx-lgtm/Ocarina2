# Changelog

All notable changes to Eglantine are documented here. Each release's notes are taken from its
`## [X.Y.Z]` section (release candidates use the section of the version they are a candidate for).
New entries go under `## [Unreleased]`; `tools/ci/bump-version.sh` turns them into a version section.

## [Unreleased]

- Reduce Ocarina of Time GPU submission and timing overhead, apply inherited title settings before
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

## [0.1.0]

- Initial release candidate
