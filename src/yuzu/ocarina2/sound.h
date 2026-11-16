// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#pragma once

#include <QObject>

namespace Ocarina2Ui {

/// Tiny synthesized UI chimes for the Switch-2-style home screen: no asset files, no downloading,
/// a few frequencies and a short envelope each, played through the platform sound system (Windows
/// plays the PCM through the Multimedia API; other platforms fall back to the console beep).
/// Safe to call before the desktop audio device is ready - every call either plays or is dropped.
class SoundPlayer : public QObject {
    Q_OBJECT
public:
    enum class Effect {
        Navigate,   ///< Tick when the focus moves one item in the dock/strip/list.
        Confirm,    ///< Soft high "ding" - a row/dock button activated.
        Back,       ///< Low "blip" - B/back.
        Launch,     ///< Rising two-note chime - the game is starting.
        TabSwitch,  ///< Quiet "tock" - settings/tab switch.
        Error,      ///< Short dull buzz - an action that cannot run.
    };
    static SoundPlayer& Instance();
    void Play(Effect effect);

private:
    SoundPlayer();
};

} // namespace Ocarina2Ui
