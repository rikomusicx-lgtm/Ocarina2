// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <QWidget>
#include "common/common_types.h"

class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QMainWindow;
class QMenu;
class QPushButton;
class QStandardItemModel;
class QTimer;
class QToolButton;

namespace Ocarina2Ui {

/// What the home screen asks the main window to do.
struct HomeHooks {
    std::function<void(const QString& path, u64 program_id)> play;
    std::function<void()> add_folder;
    std::function<void()> open_settings;
    std::function<void(u64 program_id, const std::string& path)> game_settings;
    std::function<void(u64 program_id)> delete_shader_cache; ///< after a confirmation
    /// Fills a menu with the game list's own right-click entries for the game (mod folders,
    /// dumps, Remove, Verify Integrity, Configure Game...).
    std::function<void(QMenu& menu, u64 program_id, const std::string& path)> game_menu;
    std::function<void()> toggle_fullscreen;
    std::function<bool()> game_list_empty;
    std::function<bool()> single_window;
    std::function<bool()> emulation_running;
    std::function<QWidget*()> game_view;   ///< The games list's active view (tree/grid/carousel)
    std::function<QWidget*()> filter_edit; ///< The games list's filter line edit
    QStandardItemModel* model = nullptr;
    QMenu* help_menu = nullptr;
    QList<QMenu*> classic_menus;  ///< File, Game, View, Tools, Help behind the dock's More button
};

/// The Ocarina2 main screen, drawn like the Switch 2 home menu: a top bar (player avatar,
/// Home/Games switch, network glyphs and the clock), the highlighted game with a bookmark,
/// and a row of round system buttons at the bottom on a flat, very dark purple backdrop.
/// The classic game list stays one press away ("Games"); the main window keeps managing
/// it as before.
class Home : public QObject {
    Q_OBJECT
public:
    Home(QMainWindow* window, QBoxLayout* central, QWidget* game_list, QWidget* placeholder,
         HomeHooks hooks);

    /// A game is starting in the main window: the render view takes the space.
    void OnEmulationStarting();
    /// Show the right page (home or the list) and put focus in the dock, console style.
    void Restore();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ShowHome();
    void ShowList();
    void Refresh();
    /// Cheap selection update (bookmark row + what the hero image boots).
    void SelectGame(const QString& path, u64 program_id, const QString& title);
    /// Pins the hero card to the background art's centre line.
    void FixHomeLayout();
    /// The per-game context menu (right click / X on the hero image).
    void ShowGameMenu(const QString& path, u64 program_id, const QPoint& global_pos);

    QWidget* MakeRail();
    QWidget* MakePage();
    void RebuildNavOrder();
    /// Centers the floating Back button at the bottom of the window.
    void PositionBackButton();
    bool HandleNavKey(QKeyEvent* event);
    /// Games-tab navigation: self-contained (no reliance on where the focus landed wrong).
    bool HandleListNavKey(QKeyEvent* event);

    QMainWindow* window;
    QWidget* game_list;
    QWidget* placeholder;
    HomeHooks hooks;

    QWidget* rail = nullptr;
    QWidget* page = nullptr;
    QWidget* topbar = nullptr;
    QToolButton* avatar_button = nullptr;
    QWidget* game_row = nullptr;
    QLabel* game_row_title = nullptr;
    QLabel* clock_label = nullptr;
    QTimer* clock_timer = nullptr;
    QToolButton* restore_game_button = nullptr;
    QPushButton* back_button = nullptr;  ///< floating "Back to Home" on the games screen
    QToolButton* home_button = nullptr;
    QToolButton* library_button = nullptr;
    QToolButton* folder_button = nullptr;
    QToolButton* update_button = nullptr;
    QToolButton* settings_button = nullptr;
    QToolButton* menu_button = nullptr;
    QToolButton* play = nullptr;  ///< the hero image, click to launch
    QList<QWidget*> nav_order_;  ///< Focus order: dock and hero.
    int nav_index_ = 0;
    QTimer* refresh_timer = nullptr;

    bool list_mode = false;
    QString hero_path;
    u64 hero_id = 0;
    QString hero_title;
};

} // namespace Ocarina2Ui
