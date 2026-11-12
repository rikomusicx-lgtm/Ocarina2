<!--
The "What's new" notes shown on first launch after an update (Help > What's New). Paste the
public release notes here when cutting a build. The download instructions at the top are
dropped automatically in the app.
-->
## Ocarina2 v1.1

The GUI release: Ocarina2 looks like the console it imitates, boot to games list, and the worst v1 paper cuts are fixed.

**New GUI**
- **Hyrule home screen.** The home page is a built-in twilight scene of Hyrule Castle, drawn into the window. To use your own art instead, drop a `background.png` (or .jpg / .jpeg, up to 16 MB) into your ocarina2 folder and restart.
- **Switch 2-style dock.** Home, Games, Add games, Settings, What's new and More across the bottom with the bubbly hover/focus bounce. The classic menus live behind More; the bar stays out of the game's way.
- **Top bar.** Triforce avatar, Home/Games switch, network glyphs and clock, aqua on purple throughout — the title is aqua too.
- **Games pane.** Games opens as a pane with its filter bar; controller focus lands on the list, A/Enter launches the highlighted game, B/Escape (or the Back button) returns home.
- **What's new in-app.** This dialog, from the dock's What's new button or Help > What's New.

**Bug fixes**
- **Back button lost after the game closes.** In single-window mode the home screen and the dock stayed hidden after the game exited — you were stranded on the games list with no way home. The window now restores exactly what you had: the home screen, or the games pane with the Back button visible again.
- **Game list stacked over the home screen on some boots.** Whether the games list won or lost the race between the game-list scan finishing and the home screen restoring decided which of the two showed; the window now always restores one coherent state.
- **Games-pane controller double-fire.** The stick/D-pad could jump two rows per press with the games list and the filter bar focused; navigation is single-shot now.
- **Filter bar reading "0 of 0 result(s)".** The grid/carousel views never reported their visible counts and the flat-mode total was computed with tree semantics; the counter now shows the real visible/total on every view.
- **OoT reverting your graphics preset at boot.** The OoT performance preset pinned Area upscaling, no anti-aliasing and 3x frame generation on top of your settings on every boot. It now applies the same values the global preset bakes in (SGSR edge upscaling, FXAA, Optical Flow x4), and the per-game overrides defer to the global profile.
- **Hero card placement** is anchored to the artwork slot on every window size.

**Performance and defaults**
- The v1 graphics preset is now the default for new installs: Optical Flow (FSR3) frame generation x4, SGSR edge upscaling, FXAA, immediate vsync, Vivid post shader profile (off).
- Built for low-end PCs: the 8 GB memory layout stays on machines under 16 GB of RAM (12 GB only where it fits), handheld mode, BC3 ASTC textures and conservative VRAM for the texture cache — 8 GB cards should stay well under 4 GB of VRAM for this game.
- The built-in Dynamic FPS patch keeps Ocarina of Time at full speed when the frame rate drops — the game no longer slows to a crawl at low FPS. If your PC can't hold 60, prefer 30 FPS mode (status bar toggle) and frame generation over the 60 FPS mode to avoid the hitching that comes with it.

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
