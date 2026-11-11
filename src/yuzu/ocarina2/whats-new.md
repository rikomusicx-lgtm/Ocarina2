<!--
The "What's new" notes shown on first launch after an update (Help > What's New). Paste the
public release notes here when cutting a build. The download instructions at the top are
dropped automatically in the app.
-->
## Ocarina2 v1

The first real release. Built on Frizti's Fritchii 5.8.4 - Ocarina2 is its own fork: its own
profile folder, its own branding and updates, and the Ocarina of Time build we want people to
actually play, not a side project of anyone else's release.

Different from Fritchii:
- **Your own profile.** Saves and settings live in `%APPDATA%\ocarina2`. First launch offers to
  migrate everything from an eden / Fritchii / Lichii / yuzu / suyu / sudachi / citron install,
  so your saves and games are exactly where they were. Help > Copy Data does the same by hand.
- **Ocarina2 everywhere, nothing else.** Own window title, icon and menus. Discord and Donate
  are gone; the Saves button stays. Help > Export Log gives us the one file we need to fix a
  crash.
- **Frame generation you can use in game.** F12 or L3+R3 > Settings > Frame generation flips
  between Optical Flow x3 and Lossless Scaling (LSFG) x3 while you play - it saves. The FG
  button in the status bar does the same (left-click on/off, right-click to pick). Optical Flow
  is the built-in option and needs no extra files; LSFG is sharper when your GPU keeps up.
- **LSFG just works.** If you own Lossless Scaling on Steam, Ocarina2 finds it and installs its
  DLL automatically on first use - nothing to download, nothing to copy. Lost it? Right-click
  the FG button > Install Lossless.dll from Steam. No Lossless Scaling? Optical Flow is built
  in and needs nothing. The release never ships Lossless.dll itself - it's not ours to
  redistribute.
- **The OoT performance path that made Kokiri playable, shipped as its own release.** The
  12 GB emulated memory layout that fixed the streaming stutter on 16 GB+ machines (8 GB stays
  on lower-end PCs), the handheld-tuned OOT 60 FPS preset, the warm shader cache, the FSR
  anti-aliasing options, DLSS off by default. Detail is never dropped for speed.
- **Real merge fixes for the build** (windows sdk resources, built-pipeline overload, union
  brace) so the fork actually ships clean.

Still true and still ours to say: this build improves **performance and stuttering** - how the
game actually plays, day to day. **It is not intended to improve your FPS.**

We know there are still bugs, and we are working on them. If you hit one, send us your log:
Help > Export Log.

This is build Ocarina2 v1.

Not affiliated with Nintendo. Built on Eglantine, Eden, Lichii and Frizti's Fritchii (5.8.4).
