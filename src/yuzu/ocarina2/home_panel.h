// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#pragma once

#include <functional>
#include <vector>

#include <QDialog>

#include "common/common_types.h"

class QVBoxLayout;
class QHBoxLayout;
class QAbstractScrollArea;
class QLabel;
class QPushButton;
class QScrollArea;
class QWidget;

namespace Ocarina2Ui {

/// A rounded in-page panel in the spirit of the Switch 2's own system menus: a dark card with
/// an eyebrow caption, a title, and a list of rows that behave like them - keyboard navigable,
/// activated with the A button, closed with B. Every dock button that used to pull up a window
/// opens one of these instead, so Home never leaves the page.
class HomePanel : public QDialog {
    Q_OBJECT
public:
    HomePanel(QWidget* parent, const QString& eyebrow, const QString& title);

    /// A small caption above a group of rows ("GRAPHICS", "SYSTEM", ...).
    void AddSection(const QString& text);
    /// A row that runs an action when pressed (A / Enter / click).
    void AddRow(const QString& icon, const QString& label, std::function<void()> on_activate,
                bool enabled = true);
    /// A row with a value on the right; A or Left/Right cycles it, exactly like the quick menu.
    void AddValueRow(const QString& icon, const QString& label, std::function<QString()> value,
                     std::function<void(int)> cycle, std::function<bool()> enabled = nullptr);
    /// A large scrolling body (e.g. the What's New notes) inside the card.
    void SetBodyWidget(QWidget* widget);
    /// A hint line at the bottom ("A Select   B Back").
    void SetHints(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct ValueRow {
        QPushButton* button = nullptr;
        QString label;
        std::function<QString()> value;
        std::function<void(int)> cycle;
        std::function<bool()> enabled;
    };

    void focusFirstRow();
    void refreshValueRows();
    void cycleFocusedRow(int direction);

    QWidget* panel = nullptr;        ///< the card
    QScrollArea* scroll = nullptr;   ///< rows area (rows mode)
    QVBoxLayout* rows = nullptr;     ///< rows area layout (rows mode)
    QWidget* body_ = nullptr;        ///< body widget (body mode)
    QAbstractScrollArea* body_scroll_ = nullptr;  ///< the scrollable area inside the body, if any
    QVBoxLayout* card_layout = nullptr;
    QLabel* hints_label = nullptr;
    std::vector<QPushButton*> row_buttons_;
    std::vector<ValueRow> value_rows_;
};

} // namespace Ocarina2Ui
