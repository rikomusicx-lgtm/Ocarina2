// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#pragma once

#include <functional>

#include <QObject>

class QWidget;

namespace Ocarina2Ui {

/// Makes a settings-style modal fully usable with a controller, Switch-2 style:
///   D-pad / left stick   = move focus between every control
///   A / Enter / Space    = activate (toggles checkboxes, opens combos, steps spinboxes)
///   Left / Right         = adjust the focused value widget (combo / spinbox / slider)
///   B / Escape           = the screen's back action (reject by default)
/// The keys arrive via ControllerNavigation, which posts them to the modal widget. Focus is
/// moved by synthesizing Tab/BackTab so the whole dialog behaves like one console menu.
class ControllerDialogFilter : public QObject {
    Q_OBJECT

public:
    explicit ControllerDialogFilter(QObject* parent = nullptr) : QObject(parent) {}

    /// PageUp/PageDown handler (the Configure dialog cycles its category list); optional.
    std::function<void(int delta)> page_cycle;
    /// B/Escape handler; defaults to reject() on the parent dialog.
    std::function<void()> escape;

    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Installs the filter on this widget and every descendant (like the dialogs do).
    static void InstallRecursive(QWidget* root, QObject* filter);

private:
    static void moveFocus(QWidget* focus, bool forward);
};

} // namespace Ocarina2Ui
