# Ocarina2 branding

Ocarina2 (rikomusicx-lgtm/Ocarina2) is The Legend of Zelda: Ocarina of Time edition of Lichii,
built on top of zortos293's and Frizti's forks. Its own look — the name, the logo and the Ocarina2
UI — is kept apart from emulator work.

## Telling branding from fixes

Branding-only commits carry the trailer `Ocarina2-Branding: yes` and touch nothing that changes
emulation. Everything else — the fritchii v5.4 merge, the water and DMA-drain fixes, the debug
server, the split-GPU work — is emulator work.

## What branding means here

- `dist/ocarina2.ico`, `dist/ocarina2.png`, `dist/ocarina2.icns`: the fork's artwork (logo art by
  Frizti).
- `src/yuzu/yuzu.rc`, `src/yuzu_cmd/yuzu.rc`, `src/dedicated_room/yuzu_room.rc`,
  `src/yuzu/yuzu.qrc`, `dist/qt_themes/default/default.qrc`: the build's icons point at them.
- `dist/ocarina2_version.txt`, `CMakeModules/GenerateSCMRev.cmake`, `src/qt_common/qt_common.cpp`:
  the version stamped into the window title and the log (`Ocarina2 ...`).
- `src/yuzu/ocarina2/`: the Ocarina2 UI layer (home screen, top bar, in-game strip, What's New,
  the Graphite/switch-2 palette) and its assets.
- `docs/ocarina2/`: this folder.

User data stays in the shared Eden/Eglantine folder (`%APPDATA%\eden`), so settings, saves and
shader caches carry over from the other builds.
