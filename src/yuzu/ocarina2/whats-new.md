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

### Fritchii v5.5-v5.8.4 (merged in full, build-122-dev)

The whole Fritchii v5.5-v5.8.4 line is in this build, wholesale, on top of the earlier Fritchii v5.4:

- **AMD and NVIDIA are both fully covered now:** new shader-preparation switches (async read-back on by default, zero-trim idle timing, the hot/clean tracked-pages collector ceilings and the 7680 MiB texture-cache ceiling), hangs fixed on AMD's mesh path, the Fritchii profile move (%APPDATA%\fritchii, imported once on first launch), saves backups before every overwrite, the Saves window (export, import, copy between Ocarina2, Eden and Lichii) and Help > Copy Data.
- **NVIDIA DLSS picks its mode per game now** (Automatic, Native (DLAA), Quality, Balanced, Performance, Ultra Performance, validated against NVIDIA's range), preset per game too (Default, J, K, L, M). Settings > General has Reset to Defaults (with a timestamped backup under config/backups/, kept at ten) and Restore Settings; per-game overrides are bannered with one-click reset to global.
- **Settings backups, Saves window, Help > Copy Data and per-game DLSS are all reachable from the home top bar and the Help menu**; logging says what caused each stutter so reports are much easier to track down.
- Our own changes this round: the Fritchii v5.4 AMD prep stays, the OOT 60 FPS preset (FG x3 default, adaptive 12 GB emulated DRAM on 16 GB+ hosts, FG and Ctrl+U and the new upstream high-refresh spot that coexists with the Hyrule Cheat Menu), Kokiri water fixes and the Ocarina white flash investigation (still in progress, see the notes).
- Fritchii's own tuned settings are applied once more with this build; you can change anything afterwards. Credits for all of the above: Frizti's Fritchii line.

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
