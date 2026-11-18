// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShowEvent>

#include "common/settings.h"
#include "qt_common/config/shared_translation.h"

class QWidget;
class QPushButton;
class QStackedWidget;
class QScrollArea;
class QLabel;

namespace Ocarina2Ui {

/// Hooks handed in by the main window: how to reach the rest of the settings world. The full
/// OC settings store is read and written through common/settings.h directly.
struct ConsoleSettingsHooks {
    /// Apply the changed settings to running emulation + persist them.
    std::function<void()> apply_live;
    /// Emulation is running: some rows lock while in-game (e.g. renderer).
    std::function<bool()> emulation_running;
    /// Open the OC configuration dialog on the given tab (0=General, 1=Graphics, 2=Controls,
    /// 3=Audio, 4=System, 5=Advanced).
    std::function<void(int)> open_configure;
};

class ConsoleSettings : public QDialog {
    Q_OBJECT

public:
    explicit ConsoleSettings(QWidget* parent, ConsoleSettingsHooks hooks);
    ~ConsoleSettings() override;

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        QPushButton* button = nullptr;
        std::function<QString()> text;   ///< what the row shows ("label . value")
        std::function<void(int)> on_cycle;
        std::function<bool()> enabled;
        int page_index = 0;              ///< which page this row belongs to
    };

    QWidget* MakeScrollPage(const QString& title, const QString& eyebrow);
    void AddRow(QWidget* page, const QString& icon, const QString& label,
                std::function<QString()> value, std::function<void(int)> on_cycle,
                bool enabled = true, const QString& toolTip = QString(), int page_index = -1);
    void AddActionRow(QWidget* page, const QString& icon, const QString& label,
                      std::function<void()> on_trigger, bool closes = true, int page_index = -1);
    void AddSettingRow(QWidget* page, Settings::BasicSetting* setting, const QString& icon,
                       int page_index, bool unlock_when_emulation = false);

    void Cycle(Settings::BasicSetting* setting, int direction);

    std::unique_ptr<ConfigurationShared::TranslationMap> translations;
    std::unique_ptr<ConfigurationShared::ComboboxTranslationMap> combobox_translations;
    ConsoleSettingsHooks hooks;

    QStackedWidget* pages_ = nullptr;
    QWidget* panel_ = nullptr;
    std::vector<Row> rows_;
    int current_page_ = 0;

    void RefreshRows();
    void ShowPage(int page);
    void BuildPages();
    void HandleKey(Qt::Key key);
    void InstallRecursive(QWidget* root, QObject* filter);
    /// Scrolls the focused row into view (the rows live in a QScrollArea per page; the focus
    /// ring otherwise walks off the bottom of the page out of sight).
    void EnsureFocusVisible(QWidget* widget);
};

} // namespace Ocarina2Ui
