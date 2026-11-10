// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QDialog>

class QVBoxLayout;
class QLabel;
class QPushButton;

namespace Ocarina2Ui {

/// The L3+R3 "pause menu" for the emulator: a console-style list of everyday emulator
/// actions — resume, frame rate, fullscreen, a screenshot, the settings, restart and stop —
/// shown over a running (or idle) session. Deliberately small: it is the emulator's menu,
/// not the game's.
struct QuickMenuHooks {
    bool in_game = true;  ///< restart/stop are disabled without a running game
    std::function<void()> toggle_frame_rate;
    std::function<void()> toggle_fullscreen;
    std::function<void()> screenshot;
    std::function<void()> open_settings;
    std::function<void()> restart;
    std::function<void()> stop;
};

class QuickMenu : public QDialog {
    Q_OBJECT

public:
    explicit QuickMenu(QWidget* parent, QuickMenuHooks hooks);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void buildPanel();

    QuickMenuHooks hooks;
    QWidget* panel = nullptr;
    QPushButton* resume_button = nullptr;
};

} // namespace Ocarina2Ui
