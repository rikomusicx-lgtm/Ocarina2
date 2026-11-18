// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#include "yuzu/ocarina2/controller_dialog.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QDialog>
#include <QKeyEvent>
#include <QWidget>

#include "yuzu/ocarina2/sound.h"

namespace Ocarina2Ui {

void ControllerDialogFilter::InstallRecursive(QWidget* root, QObject* filter) {
    root->installEventFilter(filter);
    const auto children = root->children();
    for (QObject* child : children) {
        if (auto* widget = qobject_cast<QWidget*>(child)) {
            InstallRecursive(widget, filter);
        }
    }
}

void ControllerDialogFilter::moveFocus(QWidget* focus, bool forward) {
    QWidget* target = focus ? focus->parentWidget() : nullptr;
    if (!target) {
        return;
    }
    // A synthetic Tab/BackTab runs Qt's own focus traversal; nothing else consumes it here.
    auto* event =
        new QKeyEvent(QEvent::KeyPress, forward ? Qt::Key_Tab : Qt::Key_Backtab,
                      Qt::NoModifier);
    QApplication::sendEvent(target, event);
}

bool ControllerDialogFilter::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        // The pill row behind the focused control lights up (rowFocused property + qss).
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            QWidget* row = widget;
            for (int hops = 0; row && hops < 8; ++hops) {
                if (row->objectName() == QLatin1String("Ocarina2SettingRow")) {
                    break;
                }
                row = row->parentWidget();
            }
            if (row && row->objectName() == QLatin1String("Ocarina2SettingRow")) {
                row->setProperty("rowFocused", event->type() == QEvent::FocusIn);
                row->style()->unpolish(row);
                row->style()->polish(row);
            }
        }
        return QObject::eventFilter(watched, event);
    }
    if (event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }
    auto* key = static_cast<QKeyEvent*>(event);
    const int key_code = key->key();
    QWidget* focus = QApplication::focusWidget();

    auto* combo = qobject_cast<QComboBox*>(focus);
    auto* spin = qobject_cast<QAbstractSpinBox*>(focus);
    auto* slider = qobject_cast<QAbstractSlider*>(focus);
    auto* button = qobject_cast<QAbstractButton*>(focus);
    auto* list = qobject_cast<QAbstractItemView*>(focus);

    // The category selector list keeps its native arrows (each change rebuilds its page).
    if (list && (key_code == Qt::Key_Up || key_code == Qt::Key_Down)) {
        return false;
    }

    // PageUp/PageDown: the dialog may use them for its own navigation (e.g. category list).
    if (key_code == Qt::Key_PageUp || key_code == Qt::Key_PageDown) {
        if (page_cycle) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            page_cycle(key_code == Qt::Key_PageUp ? -1 : 1);
            return true;
        }
        return false;
    }

    // A / Enter / Space: activate the focused control like the console's confirm key.
    if (key_code == Qt::Key_Return || key_code == Qt::Key_Enter || key_code == Qt::Key_Space) {
        if (button) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
            button->click();
            return true;
        }
        if (combo) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
            combo->showPopup();
            return true;
        }
        if (spin) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            spin->stepUp();
            return true;
        }
        if (slider) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            slider->setValue(slider->value() + slider->singleStep());
            return true;
        }
        return true;  // swallow stray confirms so they never leak into a hidden control
    }

    // Left / Right: adjust the focused value widget; otherwise walk focus horizontally.
    if (key_code == Qt::Key_Left || key_code == Qt::Key_Right) {
        const int direction = key_code == Qt::Key_Left ? -1 : 1;
        if (auto* checkbox = qobject_cast<QCheckBox*>(focus)) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            checkbox->click();
            return true;
        }
        if (combo) {
            const int count = combo->count();
            if (count > 1) {
                SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
                combo->setCurrentIndex((combo->currentIndex() + direction + count) % count);
            }
            return true;
        }
        if (spin) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            if (direction > 0) {
                spin->stepUp();
            } else {
                spin->stepDown();
            }
            return true;
        }
        if (slider) {
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            slider->setValue(slider->value() + direction * slider->singleStep());
            return true;
        }
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        moveFocus(focus, direction > 0);
        return true;
    }

    // Up / Down: walk the focus chain like the console's d-pad.
    if (key_code == Qt::Key_Up || key_code == Qt::Key_Down) {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        moveFocus(focus, key_code == Qt::Key_Down);
        return true;
    }

    // B / Escape: back.
    if (key_code == Qt::Key_Escape) {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Back);
        if (escape) {
            escape();
            return true;
        }
        if (auto* dialog = qobject_cast<QDialog*>(parent())) {
            dialog->reject();
            return true;
        }
        return true;
    }

    return QObject::eventFilter(watched, event);
}

} // namespace Ocarina2Ui
