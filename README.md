<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<h2 align="center">⚠️ Lichii has no official website ⚠️</h2>

> [!CAUTION]
> **Lichii has no official website.** The only site run by this project is the **game compatibility website, [lichii.zortos.me](https://lichii.zortos.me)**, for compatibility reports only. Download Lichii **only** from this repository's [releases page](https://github.com/zortos293/Lichii/releases). Any other "Lichii" website or download (such as lichii.org) is not ours and may contain malware ([#545](https://github.com/zortos293/Lichii/issues/545)).

<p align="center">
  <img src="dist/ocarina2.png" alt="Ocarina2" width="600">
</p>

# Ocarina2

Ocarina2 is our own fork of the Eglantine/Eden family — built on Lichii and Frizti's Fritchii — made to do one game justice: The Legend of Zelda: Ocarina of Time. An experimental, open-source Nintendo Switch and Switch 2 emulator written in C++.

This is an independent fork, not an official release of those upstream projects. Ocarina2 is not affiliated with Nintendo.

There is no official Ocarina2 website. Only download Ocarina2 from this repository's releases page; anything else offering an "Ocarina2" download is not ours.

## Downloads and setup

Published builds belong on this fork's [releases page](https://github.com/rikomusicx-lgtm/Ocarina2/releases). Ocarina2 has no other download site: archives offered elsewhere are not built by this project.

On Windows, extract the entire build archive and run **Ocarina2.exe**. Only use games and system files you have legally obtained. Games, keys and firmware are not included.

### Existing saves and settings

Ocarina2 keeps its own profile: `%APPDATA%\ocarina2` on Windows, and the `ocarina2` directories under `$XDG_DATA_HOME`, `$XDG_CONFIG_HOME` and `$XDG_CACHE_HOME` on Linux and macOS. Eden, Fritchii, Lichii, yuzu, suyu, sudachi and citron profiles are all still picked up as import sources (see below). An existing portable `user/` directory still takes priority and is not imported or changed.

On the first launch the emulator copies your newest existing profile (an earlier Fritchii or Eden in `%APPDATA%\eden`, Lichii 150+ in `%APPDATA%\lichii`, else yuzu, suyu, sudachi or citron) into its own folder and shows what it copied. Settings, keys, mods and saves are copied; caches are not, so shaders are prepared again once. The old folder is never changed. If the emulator using it is still open, close it and start Ocarina2 again. An interrupted copy is put back and retried on the next launch. Back up your saves before testing experimental builds.

## Compatibility and issue reports

Compatibility and performance vary by game, hardware and driver. Rendering errors, stutters and crashes remain possible. Passing automated tests does not establish that a game renders correctly, and this project makes no universal frame-rate guarantee.

When [reporting an issue](https://github.com/rikomusicx-lgtm/Ocarina2/issues), include:

- The exact build (`Ocarina2 Version: ...` from the first line of `eden_log.txt`), game version, CPU, GPU and driver version.
- Relevant settings and steps to reproduce the problem.
- A screenshot and the log from the affected session, after checking it for personal information.

Do not upload game files, keys, firmware or private account information.

## Building and contributing

See the [build guide](docs/Build.md), [user documentation](docs/user/README.md) and [coding guidelines](docs/Coding.md). Some inherited documentation and internal build target names still use upstream names. The GUI build target remains `yuzu`; its output is `Ocarina2.exe` on Windows and `ocarina2` on other desktop platforms.

Keep changes focused and distinguish code-level checks from actual gameplay testing.

## Artwork

Art created by YOB (yobslab). The fork's logo and icon artwork are in [dist](dist).

## Support

Ocarina2 is a free, unofficial fan project and does not fundraise. If you want to support the emulator this fork builds on, support the upstream [Lichii](https://github.com/zortos293/Lichii) project instead.

## Credits and license

Ocarina2 inherits the work of the Eglantine, Eden, yuzu and Sudachi projects and their contributors, including Eglantine's original developer Camille LaVey. It also carries the Lichii fork work of zortos293 and the Fritchii (Ocarina2-line) fork by Frizti, leonardochappuis, CheezerWheezer and koriko, and the community PRs credited in the release notes. Original copyright notices and third-party licenses are retained.

See [LICENSE.txt](LICENSE.txt) and the source files for applicable license terms.
