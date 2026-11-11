// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>
#include <QDialog>

#include "common/common_types.h"

class QVBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace Ocarina2Ui {

/// The L3+R3 "pause menu" for the emulator: a console-style list of everyday emulator
/// actions - resume, frame rate, fullscreen, a screenshot, settings, restart and stop -
/// shown over a running (or idle) session. The settings page edits the very same
/// Settings::values store the full Configure window reads and writes, so both interfaces
/// always agree, and everything applies live like the status bar buttons. Deliberately
/// small: it is the emulator's menu, not the game's.
struct QuickMenuHooks {
    bool in_game = true;  ///< restart/stop are disabled without a running game
    /// Raw Switch npad button mask (Core::HID::NpadButton bits; A = 1<<0 ... Down = 1<<15).
    /// Read on the GUI thread every frame the menu is open. The emulated controller's button
    /// state is updated straight from the SDL driver, so this covers every pad the home
    /// screen can see and keeps working while the emulated CPU is paused - unlike the HID
    /// callbacks. Return 0 when the pad is unavailable.
    std::function<u64()> npad_buttons;
    std::function<void()> toggle_frame_rate;
    std::function<void()> toggle_fullscreen;
    std::function<void()> screenshot;
    std::function<void()> restart;
    std::function<void()> stop;

    /// Settings page (in-menu). Every row reads the current value from the same store the
    /// Configure window reads (synced) and applies exactly like the status bar buttons do.
    std::function<QString()> console_mode_text;  ///< e.g. DOCKED / HANDHELD / GPU DOCKED
    std::function<void()> toggle_docked_mode;    ///< same action as the dock (console/handheld)
    std::function<QString()> renderer_text;      ///< e.g. VULKAN; locked while a game runs
    std::function<bool()> renderer_locked;       ///< true while emulation is running
    std::function<void(int direction)> cycle_renderer;
    std::function<QString()> gpu_accuracy_text;
    std::function<void(int direction)> cycle_gpu_accuracy;
    std::function<QString()> scaling_filter_text;
    std::function<void(int direction)> cycle_scaling_filter;
    std::function<QString()> anti_aliasing_text;
    std::function<void(int direction)> cycle_anti_aliasing;
    std::function<QString()> frame_gen_text;       ///< e.g. LSFG / OPTICAL FLOW
    std::function<void(int direction)> cycle_frame_gen;
};

class QuickMenu : public QDialog {
    Q_OBJECT

public:
    explicit QuickMenu(QWidget* parent, QuickMenuHooks hooks);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

public slots:
    void HandleKey(Qt::Key key);

private slots:
    void PollController();  ///< Polls hooks.npad_buttons; HID callbacks stall while the game is paused.

private:
    enum class Page { Main, Settings };

    void showPage(Page page);
    void RefreshSettingsRows();
    void cycleFocusedRow(int direction);
    QPushButton* addSettingsRow(QVBoxLayout* layout, const QString& icon, const QString& label,
                                std::function<QString()> value_text,
                                std::function<void(int direction)> cycle,
                                std::function<bool()> enabled = nullptr);

    static void InstallRecursive(QWidget* root, QObject* filter);

    QuickMenuHooks hooks;
    QWidget* panel = nullptr;
    QLabel* title_label = nullptr;
    QPushButton* resume_button = nullptr;
    QPushButton* settings_button = nullptr;  ///< "Settings" row on the main page
    QStackedWidget* pages = nullptr;
    struct SettingsRow {
        QPushButton* button = nullptr;
        QString label;
        std::function<QString()> value_text;
        std::function<void(int direction)> cycle;
        std::function<bool()> enabled;
    };
    std::vector<SettingsRow> settings_rows;
    QTimer* controller_timer = nullptr;
    u64 prev_npad_buttons = 0;  ///< Edge detection for the polled npad mask
};

} // namespace Ocarina2Ui
