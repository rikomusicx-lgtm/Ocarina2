<!--
The "What's new" notes shown on first launch after an update (Help > What's New). Paste the
public release notes here when cutting a build. The download instructions at the top are
dropped automatically in the app.
-->
## What's new in Ocarina2 (this build)

This developer build is **Ocarina2** — the Legend of Zelda: Ocarina of Time on the Lichii fork of Eglantine/Eden. It is not affiliated with Nintendo.

### Since build-121 (unreleased)

- **Fritchii v5.4 merged** (Frizti's fork): AMD "Preparing shaders" now prepares the whole game, with precompiled pipelines recorded with a dynamic logic operation build where it is static, and shader library use detected from the driver's vendor instead of by driver version strings.
- **Split-GPU DMA drain fix** (`cea0eab1d`): front DMA reads now drain for queued graphics draws — this stopped Kokiri Forest's water going black in bursts (build-121).
- **Zora's River waterfall flicker fix**: handle words pointing at a storage buffer bound a null texture instead of an unrelated one; caught with `EDEN_NX2_HANDLE_TRACE`.
- **Frame generation is gone entirely**: the interpolation chains removed in build-121 were restored during the Fritchii merge by mistake and were removed again here (restore commit `1b82bac5` covers what had to be kept: the FSR3 frame interpolation shaders kept for correctness).
- **The app is Ocarina2 now**: new name, icon, window title, log line, updater target (`rikomusicx-lgtm/Ocarina2`), one unified look built for the game's home screen, and natively drawn icons (no SVG module in the CI Qt).

### Ocarina2 release history

- **build-121** (2026-11-09) — Kokiri water black bursts fixed (GPU-thread-split race); Zora's River waterfall flicker fixed; frame generation removed; DLSS Mode gauge (experimental; NGX refuses stable frames at this resolution on 6 GB cards); Switch 2 Pro Controller recognized on Windows; AMD workarounds carried as-is from upstream. Tested with 0400486024DEC000.
- **build-118** (2026-11-08) — upstream Lichii build-117 merged in full (guest tensor HMMA on cooperative matrices, OoT "No sharpening" anti-aliasing bypass, suspend-channel polling 1 ms → 50 µs, mesh programs as compute); cutscene audio fix; Midnight Purple default theme; build number in the title.
- **3050 Update** (2026-11-07) — Hyrule Field foliage crash fixed (pointer slots); cutscene video with dav1d; white haze and violet halos fixed; DLSS Super Resolution for the OoT upscaler; boot-time pipeline preloading.
- **3050 Performance** (2026-11-06) — the OOT 60 FPS preset; Frizti v3 performance ports (DMA snapshot, host frame pacer, async discard, thread pinning).

### Credits

- **Eglantine** — Camille LaVey (original developer) and contributors
- **Eden** — the Eden team, continuation of yuzu; **yuzu** — yuzu-emu; **Sudachi** — Man Sorahia and contributors
- **Lichii** — zortos293 (maintainer) and contributors; Djihads80
- **Fritchii / the Ocarina2 line** — Frizti (lead: AMD shader preparation, the Graphite UI this build's look descends from), leonardochappuis (Linux, water), CheezerWheezer (AMD), koriko (the log that pinned it)
- **Community** — janvitos, ProphetWild, 101Leafy, nyakuoff, monsterlag, Dark (Dynamic FPS), EvilOOT (DMA snapshot)
- **Artwork** — YOB (yobslab); **Ocarina of Forks Discord**
- **This fork** — rikomusicx-lgtm/Ocarina2, with the testers and bug reporters

Everyone above deserves the credit; the remaining bugs are this fork's.
