<!--
The "What's new" notes shown on first launch after an update (Help > What's New). Paste the
public release notes here when cutting a build. The download instructions at the top are
dropped automatically in the app.
-->
## Ocarina2 v1.4 (in development)

Made by rikomusicx-lgtm.

This section is the running 1.4 work list. Nothing in it has shipped; 1.4 ships only when the
owner says so, after the items below are fixed and tested on real hardware.

**Targets for this release**

- **Hyrule Town (and first large town) texture glitch on first load** - the one the owner
  reproduced on his machine. Textures in Hyrule Town look stretched/garbage on the first entry
  after a cold boot and settle afterwards. Tracked in NX2_NOTES. Owner re-tested on the running
  1.4 build on 2026-11-13 and it still happens.
- **Title screen buttons look finicky** - the button text will fade or just look weird on the
  title screen. Owner noted 2026-11-13.
- **Title screen is not as fluid as it should be** - note taken 2026-11-13; likely related to
  the same UI/renderer path as the buttons above.
- **Lossless Scaling (LSFG) frame generation is buggy** - flagged by the owner for 1.4.
  Optical Flow (FSR3) is the reliable default in 1.3 and stays that way until LSFG is fixed.
- **Lower-hardware pass** - another round of performance tuning aimed at entry-level cards
  (RDNA 2/integrated, small-cache 6 GB cards), while keeping the same output we can hold.
- **Deeper emulation work** - bring in more upstream fixes from the Lichii family forks
  (Lichii-, Eglantine-, Fritchii lineage), careful about merge scope; CI stays green or nothing
  lands. As of 2026-11-14: the DLSS path (quality modes scale the render resolution:
  Balanced 2276x1280 / Performance 1920x1080 / Ultra Performance 1280x720), DLSS image
  sanitizing, FPS Unlock fixes (step counters, platforms, Gohma steering, the sound manager at
  60 Hz so music and voices don't stop above 60 fps), the Ocarina camera read from its raw
  buffer binding (no more upscale dropouts in the Temple of Time's camera angles), points and
  lines through the mesh compute fallback (foliage shadows), the base v1.0 game's output passes
  handled by No Sharpening, the GPU memory registry delivering listener calls in order and
  outside its lock (no more frozen sessions), and the Windows host crash report (a log line and
  eden_crash.dmp) if a build ever dies.

Bugs already carried by 1.3 (the white flash) are on the list to re-verify; the release notes
will name which ones are done.

## Ocarina2 v1.3

Made by rikomusicx-lgtm.

**Stability fixes in this build**

- **A crash when the emulator maps GPU memory at the far edge of the address space is fixed.**
  The small-page map path never rejected ranges outside the 40-bit GPU address space and walked
  its own page tables out of bounds. Now such a map is refused like everywhere else, and the
  table stays intact.
- **Moving guest memory between owners no longer corrupts it.** The memory pool's move
  constructor/assignment leaked the old backing allocation and double-freed it later (crashes
  at random points, heap corruption). Windows also never reclaimed its fallback allocation at
  all, because of an invalid free call. Both fixed; the fallback path now releases properly.
- **The OoT timing patch no longer lets the game drop into slow motion under load.** Dark's
  routine clamped the tick delta to 50 ms, so below 20 fps Kokiri Forest-style zones crawled at
  half speed. It now clamps to 100 ms (the same as Lichii's #655 FPS Unlock), so the game keeps
  real time down to ~10 fps.
- **Shaders build identically across runs again.** The built-in pipeline descriptors were
  re-aligned with what the fork actually emits, and the test suite that guards the whole
  graphics path runs green again.

**Graphics**

- **The intermittent white flash is mostly gone.** The fork identified the lead candidate
  (fence-flagged query reports read before the fence runs, and the render-target relocation
  guard the driver triggers at the same time), and recent builds carry the fixes. It can still
  show up in rare cases; keep the build updated.
- **Frame generation idles on static scenes.** The title screen, menus and loading screens
  present at the display refresh with no interpolation overhead when nothing on screen moves;
  the raw frame is presented and the chain stays built for an instant resume as soon as
  something changes.
- **Large GPUs (driver budget of 10 GiB or more) no longer clamp the texture and buffer
  caches at 3.8 GB.** That clamp was made for smaller cards and was cutting off the cache's
  ability to follow the driver's budget: Ocarina of Time's textures were rebuilt constantly
  in Kokiri Forest. Cards below that budget keep the 3.8 GB cap, as do OpenGL builds.
- **New "VRAM Cap (Performance)" setting** next to Memory Layout in Systems settings: Default
  (driver budget follower), 4 GB (the smaller-card hard cap), 6 GB or 8 GB to give the
  emulator's texture and buffer caches a bigger collector ceiling. Applies on next launch and
  is per game. Your own card reports about a 5 GB budget, so 6 GB is the natural pick.
- The SPIR-V global-memory helpers are emitted inlined again by default (measured faster),
  with DontInline available via `EDEN_NX2_SPIRV_INLINE_GLOBALS=0` for those who want it.

**Shader preloading**

- **The full Ocarina of Time shader set can be pre-built on first boot.** An OoT shader
  "recipe" for this game's version and pipeline-cache version rides the entire pipeline
  build: 135,406 pipelines across ~45,000 environments, and the recipe holds none of the
  game's own code (42,848 programs checked, zero bytes found). With a recipe in place,
  "Preparing shaders" (game list, or Emulation > Prepare Shaders Now) compiles everything
  once instead of stalling in new areas; on the owner's sessions a new area cost ~700-850
  ms of freezes, now ~40-70 ms.
- The recipe is not part of the executable; it ships with 1.3 only if the owner says so.
- "Prepare Shaders" still builds warm files from your own sessions if you would rather not.

**What still needs work (be aware before you buy or download)**

- **Ocarina of Time's white flash is mostly gone, but not always gone.** Rare cases remain.
- **Lossless Scaling frame generation (LSFG) is buggy.** Frame generation does not always behave
  with it, and the fork may present duplicate or slightly mismatched frames in some scenes.
  This is known and will be fixed in the next update; Optical Flow (FSR3) is the reliable
  default.

Made by rikomusicx-lgtm. Built on Eglantine, Eden, Lichii and Frizti's Fritchii (5.8.4).
Not affiliated with Nintendo. No games included.

## Ocarina2 v1.2

Made by rikomusicx-lgtm.

The v1.2 update: the "Ocarina2 v1" scale of fixes, kept short on purpose.

**What's different compared to v1.1**

- **Enter/Space handling works on every platform.** The pad's A button now ticks checkboxes and
  radio buttons properly on Linux and macOS too — the filter used to exist only on Windows.
- **The window names the build correctly**: the app reports "Ocarina2 1.2" instead of the
  internal build number used by the release tooling.
- **Build and test-suite fixes** so the release builds are gated on the full test suite again
  (frame pacing, host memory, pipeline cache and command-fetch tests were all stale against the
  reworked engine and now pass on GCC and clang-cl).
- **CI/release plumbing**: macOS builds are opt-in now (free arm64 runners were never getting
  acquired and the runs sat queued forever), and releases are titled automatically after the
  version the binaries were built from.

No gameplay changes in this update; Ocarina of Time plays the same.

**We know there are more bugs — we are fixing them.**

This build is better than v1.1, but it is not finished. We know there are bugs left: the
intermittent white flash in combat-heavy scenes, first-boot shader compile hitches while the
cache warms up, and anything else you hit. We will keep shipping fixes. The fastest way to
help: **Help > Export Log** and send us the file.

Made by rikomusicx-lgtm. Built on Eglantine, Eden, Lichii and Frizti's Fritchii (5.8.4).
Not affiliated with Nintendo. No games included.

## Ocarina2 v1.1

Made by rikomusicx-lgtm.

The big GUI update: a new home screen, a dock to drive the app, and the worst v1 paper cuts
ironed out. This update changes how Ocarina2 looks and feels — it is not the same app with new
paint.

**The new GUI**

- **Hyrule home screen.** The home page is a hand-drawn twilight scene of Hyrule Castle, drawn
  right into the window. Don't want it? Drop a `background.png` (or `.jpg` / `.jpeg`, up to
  16 MB) into your ocarina2 folder and restart — your own picture becomes the backdrop.
- **The Switch 2-style dock.** A row of buttons across the bottom: Home, Games, Add games,
  Settings, What's new and More, with the bubbly little bounce when you hover or land focus on
  one. The classic menus are all still there — behind More. The dock stays out of your game's
  way.
- **Top bar.** Triforce avatar, Home/Games switch, network glyphs and the clock.
- **Games pane.** Games opens as its own pane with the games list controller-ready. Move focus
  with the stick or D-pad — one press moves one row, never two — press A/Enter to launch the
  highlighted game, and B/Escape (or Back) to go home again.
- **Filter bar that tells the truth.** The filter bar and counter now show the real results on
  every view — grid and carousel included — and no longer read "0 of 0 result(s)".
- **The hero card** is anchored to the artwork slot on every window size, and the hero art and
  vignette got reworked to look right on big screens.
- **What's new in-app.** This very dialog. Open it from the dock's What's new button or Help >
  What's New.

**Buttons and controls work more fluid**

Menus answer the way you expect. The A button now works like a proper Select in checkboxes and
radio buttons — settings tick and toggle when you press it, right on the pad — and focus never
gets lost or drops out of the window. Navigation feels snappier everywhere: the dock, the games
list, the filter bar and the dialogs all react to single, clean presses.

**Open the quick menu with F12 or L3+R3**

In game, press **F12** (or **L3+R3**, both sticks clicked) to open the quick menu: frame
generation, settings and more, right over the game, no need to leave. It saves what you change.
Press it again to close. Frame generation on/off (left-click the FG button) and switching
between Optical Flow and Lossless Scaling (right-click the FG button) both work the same way.

**Bug fixes**

- **Back button lost after the game closes.** In single-window mode the home screen and the dock
  stayed hidden after the game exited — you were stranded on the games list with no way home.
  The window now restores exactly what you had: the home screen, or the games pane with the
  Back button visible again.
- **Games list stacked over the home screen on some boots.** Whichever of the two won the race
  between the game-list scan and the home restore decided what showed; the window now always
  restores one coherent state.
- **Games-pane controller double-fire.** The stick/D-pad could jump two rows per press with the
  games list and the filter bar focused; navigation is single-shot now.
- **Filter bar reading "0 of 0 result(s)".** The grid/carousel views never reported their visible
  counts and the flat-mode total was computed with tree semantics; the counter now shows the real
  visible/total on every view.
- **OoT reverting your graphics preset at boot.** The OoT performance preset pinned Area upscaling,
  no anti-aliasing and 3x frame generation on top of your settings on every boot. It now applies
  the same values the global preset bakes in (SGSR edge upscaling, FXAA, Optical Flow x4), and the
  per-game overrides defer to the global profile.
- **Hero card placement** is anchored to the artwork slot on every window size.

**Performance and defaults**

- The v1 graphics preset is now the default for new installs: Optical Flow (FSR3) frame generation
  x4, SGSR edge upscaling, FXAA, immediate vsync.
- Built for low-end PCs: the 8 GB memory layout stays on machines under 16 GB of RAM (12 GB only
  where it fits), handheld mode, BC3 ASTC textures and conservative VRAM for the texture cache —
  8 GB cards should stay well under 4 GB of VRAM for this game.
- The built-in Dynamic FPS patch now keeps Ocarina of Time at full speed even when the frame rate
  collapses: the tick delta is clamped to 100 ms per real frame (the same amount Lichii's #655
  FPS Unlock uses), up from 50 ms, so the game stays real-time down to ~10 fps instead of dropping
  into slow motion below 20 fps. If your PC can't hold 60, prefer 30 FPS mode
  (status bar toggle) and frame generation over the 60 FPS mode to avoid the hitching that comes
  with it.
- Frame generation now goes idle on static screens - the title screen, menus, loading screens: the
  presented frame is downscaled to a thumbnail and hashed every frame, and when nothing has moved
  for about a second FG stops generating interpolated frames and the raw frame is presented at its
  own pace. It resumes the instant something changes, with no rebuild or quality pop.
- New setting in Core (same place as Memory Layout): **VRAM Cap (Performance)**. Default leaves
  the current behavior alone (driver budget follower, hard 3.8 GB clamp below 10 GiB cards);
  4 GB picks the old smaller-card hard cap permanently; 6 GB or 8 GB give the emulator's texture
  and buffer caches a bigger collector ceiling. Applies on the next launch; your own machine
  reports ~5 GB of budget, so 6 GB is the natural pick, and 4 GB is what 4 GB cards want.

**We know there are more bugs — we are fixing them.**

This build is better than v1, but it is not finished. We know there are bugs left in it: the
intermittent white flash in combat-heavy scenes, first-boot shader compile hitches while the
cache warms up, and anything else you hit. We will keep shipping fixes. The fastest way to
help: **Help > Export Log** and send us the file — that is the one thing we need to fix things.

Made by rikomusicx-lgtm. Built on Eglantine, Eden, Lichii and Frizti's Fritchii (5.8.4).
Not affiliated with Nintendo. No games included.

## Ocarina2 v1

The first real release. Ocarina2 is our own fork of the Eglantine/Eden family, built on Lichii
and Frizti's Fritchii 5.8.4, made to do one game justice: The Legend of Zelda: Ocarina of Time.

Its own thing, start to finish:
- **Your own profile.** Saves and settings live in `%APPDATA%\ocarina2`. First launch offers to
  migrate everything from an eden / Fritchii / Lichii / yuzu / suyu / sudachi / citron install,
  so your saves and games are exactly where they were. Help > Copy Data does the same by hand.
- **Ocarina2 throughout.** Own window title, icon and menus. Discord and Donate are gone;
  the Saves button stays. Help > Export Log gives us the one file we need to fix a crash.
- **Frame generation you can use in game.** F12 or L3+R3 > Settings > Frame generation flips
  between Optical Flow x3 and Lossless Scaling (LSFG) x3 while you play - it saves. The FG
  button in the status bar does the same (left-click on/off, right-click to pick). Optical Flow
  is the built-in option and needs no extra files; LSFG is sharper when your GPU keeps up.
- **LSFG just works.** If you own Lossless Scaling on Steam, Ocarina2 finds it and installs its
  DLL automatically on first use - nothing to download, nothing to copy. Right-click the FG
  button and choose "Install Lossless.dll from Steam" anytime. No Lossless Scaling? Optical
  Flow is built in and needs nothing. The release never ships Lossless.dll itself - it's not
  ours to redistribute.
- **The OoT performance path that made Kokiri playable, shipped as its own release.** The
  12 GB emulated memory layout that fixed the streaming stutter on 16 GB+ machines (8 GB stays
  on lower-end PCs), the handheld-tuned OOT 60 FPS preset, the warm shader cache, the FSR
  anti-aliasing options, DLSS off by default. Detail is never dropped for speed.
- **Real merge fixes for the build** (Windows SDK resources, built-pipeline overload, union
  brace) so the fork actually ships clean.

Still true and still ours to say: this build improves **performance and stuttering** - how the
game actually plays, day to day. **It is not intended to improve your FPS.**

We know there are still bugs, and we are working on them. If you hit one, send us your log:
Help > Export Log.

This is build Ocarina2 v1.

Not affiliated with Nintendo. Built on Eglantine, Eden, Lichii and Frizti's Fritchii (5.8.4).
