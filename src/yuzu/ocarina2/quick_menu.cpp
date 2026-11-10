// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/quick_menu.h"

#include <QBoxLayout>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "yuzu/ocarina2/ocarina2_ui.h"

namespace Ocarina2Ui {

QuickMenu::QuickMenu(QWidget* parent, QuickMenuHooks hooks_) : QDialog(parent), hooks{std::move(hooks_)} {
    setObjectName(QStringLiteral("Ocarina2QuickMenu"));
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
    panel->setObjectName(QStringLiteral("Ocarina2QuickMenuPanel"));
    panel->setFixedWidth(430);
    row->addWidget(panel, 0, Qt::AlignCenter);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(26, 22, 26, 14);
    layout->setSpacing(4);

    auto* eyebrow = new QLabel(panel);
    eyebrow->setObjectName(QStringLiteral("Ocarina2QuickMenuEyebrow"));
    eyebrow->setText(QObject::tr("O C A R I N A 2"));
    layout->addWidget(eyebrow);
    auto* title = new QLabel(panel);
    title->setObjectName(QStringLiteral("Ocarina2QuickMenuTitle"));
    title->setText(QObject::tr("Emulation paused"));
    layout->addWidget(title);
    layout->addSpacing(10);

    const auto addRow = [&](const QString& icon, const QString& label, const auto& on_trigger,
                            bool enabled = true, bool danger = false) {
        auto* button = new QPushButton(panel);
        button->setObjectName(danger ? QStringLiteral("Ocarina2QuickMenuItemDanger")
                                     : QStringLiteral("Ocarina2QuickMenuItem"));
        button->setText(label);
        button->setIcon(Glyph(icon, danger ? 0xff8a80 : 0xcfc9e3, 20));
        button->setIconSize(QSize(20, 20));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setEnabled(enabled);
        connect(button, &QPushButton::clicked, this, [this, on_trigger] {
            on_trigger();
            accept();
        });
        layout->addWidget(button);
        return button;
    };

    resume_button = new QPushButton(panel);
    resume_button->setObjectName(QStringLiteral("Ocarina2QuickMenuResume"));
    resume_button->setText(tr("Resume"));
    resume_button->setIcon(Glyph(QStringLiteral("play"), 0xffffff, 20));
    resume_button->setIconSize(QSize(20, 20));
    resume_button->setCursor(Qt::PointingHandCursor);
    resume_button->setFocusPolicy(Qt::StrongFocus);
    connect(resume_button, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(resume_button);

    addRow(QStringLiteral("timer"), tr("60 / 30 FPS"), hooks.toggle_frame_rate);
    addRow(QStringLiteral("fullscreen"), tr("Fullscreen"), hooks.toggle_fullscreen);
    addRow(QStringLiteral("camera"), tr("Screenshot"), hooks.screenshot);
    addRow(QStringLiteral("settings"), tr("Settings..."), hooks.open_settings);
    layout->addSpacing(6);
    addRow(QStringLiteral("restart"), tr("Restart game"), hooks.restart, hooks.in_game);
    addRow(QStringLiteral("stop"), tr("Stop game"), hooks.stop, hooks.in_game, true);

    layout->addSpacing(10);
    auto* hints = new QLabel(panel);
    hints->setObjectName(QStringLiteral("Ocarina2Hints"));
    hints->setAlignment(Qt::AlignHCenter);
    hints->setText(QObject::tr("<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
                               "<span style='color:#3fd8ff;font-weight:800'>B</span> Resume&nbsp;&nbsp;&nbsp;"
                               "<span style='color:#3fd8ff;font-weight:800'>L3+R3</span> Close"));
    layout->addWidget(hints);

    resume_button->setFocus(Qt::OtherFocusReason);
}

void QuickMenu::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Up:
            focusNextPrevChild(false);
            return;
        case Qt::Key_Down:
            focusNextPrevChild(true);
            return;
        case Qt::Key_Escape:
            event->accept();
            reject();
            return;
        case Qt::Key_Home:
            event->accept();
            reject();
            return;
        default:
            QDialog::keyPressEvent(event);
            return;
    }
}

void QuickMenu::mousePressEvent(QMouseEvent* event) {
    if (panel && !panel->geometry().contains(event->pos())) {
        reject();
        return;
    }
    QDialog::mousePressEvent(event);
}

} // namespace Ocarina2Ui
