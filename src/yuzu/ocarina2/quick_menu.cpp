// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/quick_menu.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "common/logging.h"
#include "hid_core/hid_types.h"
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
    title_label = new QLabel(panel);
    title_label->setObjectName(QStringLiteral("Ocarina2QuickMenuTitle"));
    title_label->setText(QObject::tr("Pause menu"));
    layout->addWidget(title_label);
    layout->addSpacing(10);

    // ------------------------------------------------------------ Main page
    auto* main_page = new QWidget(panel);
    auto* main_layout = new QVBoxLayout(main_page);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(4);

    const auto addRow = [&](const QString& icon, const QString& label, const auto& on_trigger,
                            bool enabled = true, bool danger = false, bool closes = true) {
        auto* button = new QPushButton(main_page);
        button->setObjectName(danger ? QStringLiteral("Ocarina2QuickMenuItemDanger")
                                     : QStringLiteral("Ocarina2QuickMenuItem"));
        button->setText(label);
        button->setIcon(Glyph(icon, danger ? 0xff8a80 : 0xcfc9e3, 20));
        button->setIconSize(QSize(20, 20));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setEnabled(enabled);
        connect(button, &QPushButton::clicked, main_page, [this, on_trigger, closes] {
            on_trigger();
            if (closes) {
                accept();
            }
        });
        main_layout->addWidget(button);
        return button;
    };

    resume_button = new QPushButton(main_page);
    resume_button->setObjectName(QStringLiteral("Ocarina2QuickMenuResume"));
    resume_button->setText(tr("Resume"));
    resume_button->setIcon(Glyph(QStringLiteral("play"), 0xffffff, 20));
    resume_button->setIconSize(QSize(20, 20));
    resume_button->setCursor(Qt::PointingHandCursor);
    resume_button->setFocusPolicy(Qt::StrongFocus);
    connect(resume_button, &QPushButton::clicked, main_page, [this] { accept(); });
    main_layout->addWidget(resume_button);

    addRow(QStringLiteral("timer"), tr("60 / 30 FPS"), hooks.toggle_frame_rate);
    addRow(QStringLiteral("fullscreen"), tr("Fullscreen"), hooks.toggle_fullscreen);
    addRow(QStringLiteral("camera"), tr("Screenshot"), hooks.screenshot);
    settings_button = addRow(QStringLiteral("settings"), tr("Settings"),
                             [this] { showPage(Page::Settings); }, true, /*danger=*/false,
                             /*closes=*/false);
    main_layout->addSpacing(6);
    addRow(QStringLiteral("restart"), tr("Restart game"), hooks.restart, hooks.in_game);
    addRow(QStringLiteral("stop"), tr("Stop game"), hooks.stop, hooks.in_game, true);

    // --------------------------------------------------------- Settings page
    auto* settings_page = new QWidget(panel);
    auto* settings_layout = new QVBoxLayout(settings_page);
    settings_layout->setContentsMargins(0, 0, 0, 0);
    settings_layout->setSpacing(4);

    // Each row reads the current value from the same store the Configure window reads and
    // writes, so both interfaces show the same thing; every change applies exactly like the
    // status bar buttons do (same Apply + save path).
    addSettingsRow(settings_layout, QStringLiteral("gamepad"), tr("Console mode"),
                   hooks.console_mode_text,
                   [this](int) {
                       if (hooks.toggle_docked_mode) {
                           hooks.toggle_docked_mode();
                       }
                   });
    // renderer_locked says "locked while a game runs" — the row's enabled predicate is
    // "may be used", so invert it here.
    const auto renderer_enabled = std::function<bool()>(
        [locked = hooks.renderer_locked] { return !locked || !locked(); });
    addSettingsRow(settings_layout, QStringLiteral("star"), tr("Renderer"), hooks.renderer_text,
                   hooks.cycle_renderer, renderer_enabled);
    addSettingsRow(settings_layout, QStringLiteral("timer"), tr("GPU accuracy"),
                   hooks.gpu_accuracy_text, hooks.cycle_gpu_accuracy);
    addSettingsRow(settings_layout, QStringLiteral("list"), tr("Upscaling filter"),
                   hooks.scaling_filter_text, hooks.cycle_scaling_filter);
    addSettingsRow(settings_layout, QStringLiteral("droplet"), tr("Anti-aliasing"),
                   hooks.anti_aliasing_text, hooks.cycle_anti_aliasing);

    auto* back_row = new QPushButton(settings_page);
    back_row->setObjectName(QStringLiteral("Ocarina2QuickMenuItem"));
    back_row->setText(tr("Back"));
    back_row->setIcon(Glyph(QStringLiteral("arrow-left"), 0xcfc9e3, 20));
    back_row->setIconSize(QSize(20, 20));
    back_row->setCursor(Qt::PointingHandCursor);
    back_row->setFocusPolicy(Qt::StrongFocus);
    connect(back_row, &QPushButton::clicked, this, [this] { showPage(Page::Main); });
    settings_layout->addWidget(back_row);

    pages = new QStackedWidget(panel);
    pages->addWidget(main_page);     // index 0
    pages->addWidget(settings_page); // index 1
    layout->addWidget(pages);

    auto* hints = new QLabel(panel);
    hints->setObjectName(QStringLiteral("Ocarina2Hints"));
    hints->setAlignment(Qt::AlignHCenter);
    hints->setText(QObject::tr("<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
                               "<span style='color:#3fd8ff;font-weight:800'>B</span> Back&nbsp;&nbsp;&nbsp;"
                               "<span style='color:#3fd8ff;font-weight:800'>L3+R3</span> Close"));
    layout->addWidget(hints);
    layout->addSpacing(4);

    RefreshSettingsRows();
    resume_button->setFocus(Qt::OtherFocusReason);
    InstallRecursive(this, this);

    // In-game the emulated CPU is paused and the HID callback chain stalls, so the menu polls
    // the raw npad button mask on the GUI thread instead — the same mask the L3+R3 shortcut is
    // driven by. Works both in-game and at home.
    if (hooks.in_game && hooks.npad_buttons) {
        controller_timer = new QTimer(this);
        controller_timer->setInterval(16);
        connect(controller_timer, &QTimer::timeout, this, &QuickMenu::PollController);
        controller_timer->start();
    }
}

QPushButton* QuickMenu::addSettingsRow(QVBoxLayout* layout, const QString& icon,
                                       const QString& label, std::function<QString()> value_text,
                                       std::function<void(int direction)> cycle,
                                       std::function<bool()> enabled) {
    auto* button = new QPushButton(layout->parentWidget());
    button->setObjectName(QStringLiteral("Ocarina2QuickMenuItem"));
    button->setIcon(Glyph(icon, 0xcfc9e3, 20));
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    connect(button, &QPushButton::clicked, this, [this, cycle](bool) {
        if (cycle) {
            cycle(+1);
        }
        RefreshSettingsRows();
    });
    layout->addWidget(button);
    settings_rows.push_back({button, label, std::move(value_text), std::move(cycle),
                             std::move(enabled)});
    return button;
}

void QuickMenu::RefreshSettingsRows() {
    for (const auto& row : settings_rows) {
        if (!row.button) {
            continue;
        }
        const QString value = row.value_text ? row.value_text() : QString();
        row.button->setText(row.label + QStringLiteral("  ·  ") + value);
        const bool is_enabled = row.enabled ? row.enabled() : true;
        row.button->setEnabled(is_enabled);
    }
}

void QuickMenu::showPage(Page page) {
    if (!pages) {
        return;
    }
    const bool to_settings = page == Page::Settings;
    pages->setCurrentIndex(to_settings ? 1 : 0);
    LOG_INFO(Frontend, "Ocarina2 quickmenu page -> {}", to_settings ? "settings" : "main");
    if (title_label) {
        title_label->setText(to_settings ? tr("Settings") : tr("Pause menu"));
    }
    if (to_settings) {
        RefreshSettingsRows();
        // Focus the first enabled row, like console menus do on page entry.
        for (const auto& row : settings_rows) {
            if (row.button && row.button->isEnabled()) {
                row.button->setFocus(Qt::OtherFocusReason);
                return;
            }
        }
        return;
    }
    if (settings_button) {
        settings_button->setFocus(Qt::OtherFocusReason);
    }
}

void QuickMenu::cycleFocusedRow(int direction) {
    for (const auto& row : settings_rows) {
        if (row.button == focusWidget() && row.cycle && row.button->isEnabled()) {
            row.cycle(direction);
            RefreshSettingsRows();
            return;
        }
    }
}

void QuickMenu::PollController() {
    const u64 buttons = hooks.npad_buttons ? hooks.npad_buttons() : 0;
    const u64 newly = buttons & ~prev_npad_buttons;
    prev_npad_buttons = buttons;
    if (newly == 0) {
        return;
    }
    LOG_INFO(Frontend, "Ocarina2 quickmenu gamepad newly={:#x}", newly);
    const auto fire = [this, newly](Core::HID::NpadButton button, Qt::Key key) {
        if (newly & static_cast<u64>(button)) {
            HandleKey(key);
        }
    };
    fire(Core::HID::NpadButton::Up, Qt::Key_Up);
    fire(Core::HID::NpadButton::Down, Qt::Key_Down);
    fire(Core::HID::NpadButton::Left, Qt::Key_Left);
    fire(Core::HID::NpadButton::Right, Qt::Key_Right);
    fire(Core::HID::NpadButton::StickLUp, Qt::Key_Up);
    fire(Core::HID::NpadButton::StickLDown, Qt::Key_Down);
    fire(Core::HID::NpadButton::StickLLeft, Qt::Key_Left);
    fire(Core::HID::NpadButton::StickLRight, Qt::Key_Right);
    fire(Core::HID::NpadButton::A, Qt::Key_Return);
    fire(Core::HID::NpadButton::B, Qt::Key_Escape);
    fire(Core::HID::NpadButton::Plus, Qt::Key_Home);
}

void QuickMenu::InstallRecursive(QWidget* root, QObject* filter) {
    root->installEventFilter(filter);
    const auto children = root->children();
    for (QObject* child : children) {
        if (auto* widget = qobject_cast<QWidget*>(child)) {
            InstallRecursive(widget, filter);
        }
    }
}

void QuickMenu::HandleKey(Qt::Key key) {
    QString focus_tag;
    if (QWidget* fw = focusWidget()) {
        focus_tag = QString::fromUtf8(fw->metaObject()->className()) + QStringLiteral("/") +
                    fw->objectName();
    }
    LOG_INFO(Frontend, "Ocarina2 quickmenu key={} focus={}", static_cast<int>(key),
             focus_tag.toStdString());
    const bool on_settings = pages && pages->currentIndex() == 1;
    switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            focusNextPrevChild(key == Qt::Key_Down);
            return;
        case Qt::Key_Left:
            if (on_settings) {
                cycleFocusedRow(-1);
                return;
            }
            focusNextPrevChild(false);
            return;
        case Qt::Key_Right:
            if (on_settings) {
                cycleFocusedRow(+1);
                return;
            }
            focusNextPrevChild(true);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            // A (the controller maps A to Enter): activate the focused row. If focus is somehow
            // elsewhere, fall back to Resume.
            if (auto* button = qobject_cast<QAbstractButton*>(focusWidget())) {
                button->click();
                return;
            }
            if (resume_button) {
                resume_button->click();
            }
            return;
        case Qt::Key_Escape:
        case Qt::Key_Home:
            // B: on the settings page it steps back to the main page; on the main page it closes.
            if (on_settings) {
                showPage(Page::Main);
                return;
            }
            reject();
            return;
        default:
            return;
    }
}

bool QuickMenu::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Up:
            case Qt::Key_Left:
            case Qt::Key_Down:
            case Qt::Key_Right:
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
            case Qt::Key_Escape:
            case Qt::Key_Home:
                HandleKey(static_cast<Qt::Key>(key->key()));
                return true;
            default:
                return QDialog::eventFilter(watched, event);
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickMenu::keyPressEvent(QKeyEvent* event) {
    HandleKey(static_cast<Qt::Key>(event->key()));
    QDialog::keyPressEvent(event);
}

void QuickMenu::mousePressEvent(QMouseEvent* event) {
    if (panel && !panel->geometry().contains(event->pos())) {
        reject();
        return;
    }
    QDialog::mousePressEvent(event);
}

} // namespace Ocarina2Ui
