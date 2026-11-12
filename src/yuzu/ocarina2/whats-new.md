<!--
The "What's new" notes shown on first launch after an update (Help > What's New). Paste the
public release notes here when cutting a build. The download instructions at the top are
dropped automatically in the app.
-->
## Ocarina2 v1.1

Made by rikomusicx-lgtm.

The big GUI update: a new home screen, a dock to drive the app, and a long list of paper cuts
from v1 ironed out. This update changes how Ocarina2 looks and feels — it is not the same app
with new paint.

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

- **Back button lost after the game closes.** In single-window mode the home screen and dock
  stayed hidden after the game exited, stranding you on the games list. The window now
  restores exactly what you had: the home screen, or the games pane with Back visible.
- **Games list sometimes stacked over the home screen on boot.** Whichever finished first won
  the race; the window now always restores one coherent state every time.
- **Games-pane navigation double-fired** (two rows per stick press) — fixed, navigation is
  single-shot now.
- **OoT was overriding your graphics preset at every boot**, forcing its own upscaling and
  anti-aliasing on top of your settings. The OoT preset now ships the same values as the
  global defaults (SGSR edge upscaling, FXAA, Optical Flow x4 frame generation), and your
  per-game overrides defer to your global profile instead of stamping over it.
- **Hero card drift** at odd window sizes — anchored now.
- **Button input across the whole app** — checkboxes, radio buttons and focused dialogs now
  respond to the pad's A button exactly like Enter/Space does on a keyboard.

**V1 performance defaults, now the default for new installs**

- Optical Flow (FSR3) frame generation x4, SGSR edge upscaling, FXAA, immediate vsync.
- 8 GB emulated memory layout under 16 GB of system RAM (12 GB where it fits), handheld mode,
  BC3 ASTC textures, conservative texture-cache VRAM for low-end PCs.
- The built-in Dynamic FPS patch keeps Ocarina of Time at full game speed at low frame rates —
  the game no longer slows to a crawl when FPS drops. If you can't hold 60, prefer 30 FPS mode
  (status bar toggle) plus frame generation.

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
