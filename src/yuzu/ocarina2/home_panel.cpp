// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#include "yuzu/ocarina2/home_panel.h"

#include <QBoxLayout>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QShowEvent>

#include <algorithm>
#include <QVariant>

#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/sound.h"

namespace Ocarina2Ui {

HomePanel::HomePanel(QWidget* parent, const QString& eyebrow, const QString& title)
    : QDialog(parent) {
    setObjectName(QStringLiteral("Ocarina2HomePanel"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    if (QWidget* top = parentWidget()) {
        setGeometry(top->geometry());
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);
    auto* row = new QHBoxLayout;
    row->addStretch(1);

    panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("Ocarina2HomePanelCard"));
    panel->setMinimumWidth(560);
    panel->setMaximumWidth(720);
    row->addWidget(panel, 0, Qt::AlignCenter);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    card_layout = new QVBoxLayout(panel);
    card_layout->setContentsMargins(26, 22, 26, 14);
    card_layout->setSpacing(4);

    auto* eyebrow_label = new QLabel(panel);
    eyebrow_label->setObjectName(QStringLiteral("Ocarina2QuickMenuEyebrow"));
    eyebrow_label->setText(eyebrow);
    card_layout->addWidget(eyebrow_label);
    auto* title_label = new QLabel(panel);
    title_label->setObjectName(QStringLiteral("Ocarina2QuickMenuTitle"));
    title_label->setText(title);
    card_layout->addWidget(title_label);
    card_layout->addSpacing(10);

    // Rows mode: a scrolling list of rows. Body mode: swap for SetBodyWidget later.
    scroll = new QScrollArea(panel);
    scroll->setObjectName(QStringLiteral("Ocarina2HomePanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* rows_widget = new QWidget(scroll);
    rows_widget->setObjectName(QStringLiteral("Ocarina2HomePanelRows"));
    rows_widget->setAttribute(Qt::WA_StyledBackground);
    rows = new QVBoxLayout(rows_widget);
    rows->setContentsMargins(2, 2, 2, 2);
    rows->setSpacing(6);
    rows->setAlignment(Qt::AlignTop);
    scroll->setWidget(rows_widget);
    card_layout->addWidget(scroll);

    hints_label = new QLabel(panel);
    hints_label->setObjectName(QStringLiteral("Ocarina2Hints"));
    hints_label->setAlignment(Qt::AlignHCenter);
    hints_label->setText(QStringLiteral(
        "<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fd8ff;font-weight:800'>B</span> Back"));
    card_layout->addWidget(hints_label);
    card_layout->addSpacing(4);

    connect(this, &QDialog::finished, this, [] {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Back);
    });
}

void HomePanel::AddSection(const QString& text) {
    auto* label = new QLabel(panel);
    label->setObjectName(QStringLiteral("Ocarina2PanelSection"));
    label->setText(text.toUpper());
    label->setContentsMargins(4, 10, 4, 2);
    if (rows) {
        rows->addWidget(label);
    }
}

void HomePanel::AddRow(const QString& icon, const QString& label, std::function<void()> on_activate,
                       bool enabled) {
    auto* button = new QPushButton(scroll->widget());
    button->setObjectName(QStringLiteral("Ocarina2PanelRow"));
    button->setText(label);
    button->setIcon(Glyph(icon, enabled ? 0xcfc9e3 : 0x54525f, 20));
    button->setIconSize(QSize(20, 20));
    button->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setEnabled(enabled);
    connect(button, &QPushButton::clicked, this, [this, on_activate](bool) {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
        if (on_activate) {
            on_activate();
        }
        // Action rows behave like the quick menu: acting closes the panel.
        accept();
    });
    if (rows) {
        rows->addWidget(button);
    }
    row_buttons_.push_back(button);
}

void HomePanel::AddValueRow(const QString& icon, const QString& label, std::function<QString()> value,
                            std::function<void(int)> cycle, std::function<bool()> enabled) {
    auto* button = new QPushButton(scroll->widget());
    button->setObjectName(QStringLiteral("Ocarina2PanelRow"));
    button->setIcon(Glyph(icon, 0xcfc9e3, 20));
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    connect(button, &QPushButton::clicked, this, [this, cycle](bool) {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
        if (cycle) {
            cycle(+1);
        }
        refreshValueRows();
    });
    if (rows) {
        rows->addWidget(button);
    }
    value_rows_.push_back({button, label, std::move(value), std::move(cycle), std::move(enabled)});
    refreshValueRows();
}

void HomePanel::SetBodyWidget(QWidget* widget) {
    if (card_layout == nullptr || widget == nullptr) {
        return;
    }
    if (scroll) {
        card_layout->removeWidget(scroll);
        scroll->setParent(nullptr);
        delete scroll;
        scroll = nullptr;
        rows = nullptr;
    }
    body_ = widget;
    widget->setObjectName(QStringLiteral("Ocarina2HomePanelBody"));
    card_layout->insertWidget(card_layout->count() - 2, widget, 1);
    setFixedWidth(std::min(940, width()));
}

void HomePanel::SetHints(const QString& text) {
    if (hints_label) {
        hints_label->setText(text);
    }
}

void HomePanel::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Same console-style pop the quick menu has: fade in with a bit of overshoot.
    const QRect target = geometry();
    setWindowOpacity(0.0);
    setGeometry(target.adjusted(target.width() / 16, target.height() / 16, -target.width() / 16,
                                -target.height() / 16));
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(150);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    auto* zoom = new QPropertyAnimation(this, "geometry", this);
    zoom->setDuration(260);
    zoom->setStartValue(geometry());
    zoom->setEndValue(target);
    zoom->setEasingCurve(QEasingCurve::OutBack);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    zoom->start(QAbstractAnimation::DeleteWhenStopped);
    focusFirstRow();
}

void HomePanel::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    const bool value_focused =
        std::any_of(value_rows_.begin(), value_rows_.end(), [this](const ValueRow& row) {
            return focusWidget() == row.button;
        });
    switch (key) {
    case Qt::Key_Up:
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        focusNextPrevChild(false);
        return;
    case Qt::Key_Down:
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        focusNextPrevChild(true);
        return;
    case Qt::Key_Left:
        if (value_focused) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            cycleFocusedRow(-1);
            return;
        }
        focusNextPrevChild(false);
        return;
    case Qt::Key_Right:
        if (value_focused) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            cycleFocusedRow(+1);
            return;
        }
        focusNextPrevChild(true);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (auto* button = qobject_cast<QPushButton*>(focusWidget())) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
            button->click();
            return;
        }
        return;
    case Qt::Key_Escape:
    case Qt::Key_Home:
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Back);
        reject();
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

void HomePanel::mousePressEvent(QMouseEvent* event) {
    // Clicking outside the card closes the panel, like the Switch's own system menus.
    if (panel && !panel->geometry().contains(event->pos())) {
        reject();
        return;
    }
    QDialog::mousePressEvent(event);
}

void HomePanel::focusFirstRow() {
    for (QPushButton* button : row_buttons_) {
        if (button && button->isEnabled()) {
            button->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    for (const ValueRow& row : value_rows_) {
        if (row.button && row.button->isEnabled()) {
            row.button->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void HomePanel::refreshValueRows() {
    for (const ValueRow& row : value_rows_) {
        const QString value = row.value ? row.value() : QString();
        row.button->setText(row.label + QStringLiteral("    â€º    ") + value);
        const bool enabled = row.enabled ? row.enabled() : true;
        row.button->setEnabled(enabled);
    }
}

void HomePanel::cycleFocusedRow(int direction) {
    for (const ValueRow& row : value_rows_) {
        if (focusWidget() == row.button) {
            if (row.cycle) {
                row.cycle(direction);
            }
            refreshValueRows();
            return;
        }
    }
}

} // namespace Ocarina2Ui