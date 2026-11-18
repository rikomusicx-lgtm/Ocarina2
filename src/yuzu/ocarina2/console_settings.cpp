// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#include "yuzu/ocarina2/console_settings.h"

#include <QBoxLayout>
#include <QDialog>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QEasingCurve>

#include <algorithm>
#include <map>
#include <vector>

#include "common/settings.h"
#include "common/settings_setting.h"
#include "common/settings_enums.h"
#include "common/logging.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/sound.h"

namespace Ocarina2Ui {
namespace {

/// The row label + tooltip for a setting, read from the OC translation table.
QString LabelFor(Settings::BasicSetting* setting,
                 const ConfigurationShared::TranslationMap& translations) {
    const auto found = translations.find(setting->Id());
    if (found == translations.cend()) {
        return QString::fromStdString(setting->GetLabel());
    }
    if (found->second.first.isEmpty()) {
        // Intentionally hidden (the dialog does the same to debug-only rows).
        return {};
    }
    return found->second.first;
}

bool IsSkippable(Settings::BasicSetting* setting,
                 const ConfigurationShared::TranslationMap& translations) {
    if (!setting) {
        return true;
    }
    if (setting->Id() == 0) {
        return true;
    }
    return LabelFor(setting, translations).isEmpty();
}

QString ValueTextFor(Settings::BasicSetting* setting,
                     const ConfigurationShared::ComboboxTranslationMap& combos) {
    if (setting->IsEnum()) {
        const auto found = combos.find(setting->EnumIndex());
        if (found != combos.cend()) {
            u32 value = 0;
            const std::string raw = setting->ToString();
            if (!raw.empty()) {
                value = static_cast<u32>(std::strtoul(raw.c_str(), nullptr, 0));
            }
            for (const auto& [id, text] : found->second) {
                if (id == value) {
                    return text;
                }
            }
        }
        return QString::fromStdString(setting->ToString());
    }
    const std::string raw = setting->ToString();
    if (raw == "true") {
        return QStringLiteral("ON");
    }
    if (raw == "false") {
        return QStringLiteral("OFF");
    }
    return QString::fromStdString(raw);
}

} // namespace

ConsoleSettings::ConsoleSettings(QWidget* parent, ConsoleSettingsHooks hooks_)
    : QDialog(parent), hooks(std::move(hooks_)) {
    setObjectName(QStringLiteral("Ocarina2ConsoleSettings"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    if (QWidget* top = parentWidget()) {
        setGeometry(top->geometry());
    }

    translations = ConfigurationShared::InitializeTranslations(this);
    combobox_translations = ConfigurationShared::ComboboxEnumeration(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);
    auto* row = new QHBoxLayout;
    row->addStretch(1);

    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("Ocarina2QuickMenuPanel"));
    panel->setFixedWidth(740);
    panel_ = panel;
    row->addWidget(panel, 0, Qt::AlignCenter);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(26, 22, 26, 14);
    layout->setSpacing(6);

    auto* eyebrow = new QLabel(panel);
    eyebrow->setObjectName(QStringLiteral("Ocarina2QuickMenuEyebrow"));
    eyebrow->setText(QStringLiteral("O C A R I N A   2"));
    layout->addWidget(eyebrow);
    auto* title = new QLabel(panel);
    title->setObjectName(QStringLiteral("Ocarina2QuickMenuTitle"));
    title->setText(QStringLiteral("Settings"));
    layout->addWidget(title);
    layout->addSpacing(8);

    pages_ = new QStackedWidget(panel);
    layout->addWidget(pages_, 1);

    layout->addSpacing(6);
    auto* hints = new QLabel(panel);
    hints->setObjectName(QStringLiteral("Ocarina2Hints"));
    hints->setAlignment(Qt::AlignHCenter);
    hints->setText(QObject::tr(
        "<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fd8ff;font-weight:800'>B</span> Back&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fd8ff;font-weight:800'>LB/RB</span> Pages"));
    layout->addWidget(hints);
    layout->addSpacing(4);

    BuildPages();
    ShowPage(0);
    InstallRecursive(this, this);
}

ConsoleSettings::~ConsoleSettings() = default;

void ConsoleSettings::InstallRecursive(QWidget* root, QObject* filter) {
    root->installEventFilter(filter);
    const auto children = root->children();
    for (QObject* child : children) {
        if (auto* widget = qobject_cast<QWidget*>(child)) {
            InstallRecursive(widget, filter);
        }
    }
}

void ConsoleSettings::BuildPages() {
    const auto addSettingPage = [this](const QString& name, const QString& eyebrow,
                                       Settings::Category category, const QString& icon) {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(name, eyebrow);
        const auto& entries = Settings::values.linkage.by_category.at(category);
        for (Settings::BasicSetting* setting : entries) {
            if (IsSkippable(setting, *translations)) {
                continue;
            }
            if (setting->Specialization() == Settings::Specialization::RuntimeList) {
                continue; // device pickers live in the full dialog
            }
            this->AddSettingRow(page, setting, icon, page_index);
        }
        pages_->addWidget(page);
    };

    // General: the controller settings button pinned to the top, then the flat UI settings.
    {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(QStringLiteral("General"),
                                       QStringLiteral("G E N E R A L"));
        AddActionRow(page, QStringLiteral("gamepad"), tr("Controller settings"), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(2); // OC Configure dialog, Controls chips selected
            }
        }, /*closes=*/true, page_index);
        const auto& entries = UISettings::values.linkage.by_category.at(
            Settings::Category::UiGeneral);
        for (Settings::BasicSetting* setting : entries) {
            if (IsSkippable(setting, *translations)) {
                continue;
            }
            this->AddSettingRow(page, setting, QStringLiteral("gamepad"), page_index);
        }
        pages_->addWidget(page);
    }
    addSettingPage(QStringLiteral("Graphics"), QStringLiteral("G R A P H I C S"),
                   Settings::Category::Renderer, QStringLiteral("star"));
    addSettingPage(QStringLiteral("Graphics Advanced"),
                   QStringLiteral("G R A P H I C S   A D V A N C E D"),
                   Settings::Category::RendererAdvanced, QStringLiteral("star"));
    {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(QStringLiteral("Graphics Extra"),
                                       QStringLiteral("G R A P H I C S   E X T R A"));
        for (Settings::BasicSetting* s : Settings::values.linkage.by_category.at(
                 Settings::Category::RendererExtensions)) {
            if (!IsSkippable(s, *translations)) {
                this->AddSettingRow(page, s, QStringLiteral("droplet"), page_index);
            }
        }
        for (Settings::BasicSetting* s : Settings::values.linkage.by_category.at(
                 Settings::Category::RendererHacks)) {
            if (!IsSkippable(s, *translations)) {
                this->AddSettingRow(page, s, QStringLiteral("droplet"), page_index);
            }
        }
        pages_->addWidget(page);
    }
    addSettingPage(QStringLiteral("CPU"), QStringLiteral("C P U"),
                   Settings::Category::Cpu, QStringLiteral("timer"));
    {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(QStringLiteral("CPU Unsafe"),
                                       QStringLiteral("C P U   U N S A F E"));
        for (Settings::BasicSetting* s : Settings::values.linkage.by_category.at(
                 Settings::Category::CpuUnsafe)) {
            if (!IsSkippable(s, *translations)) {
                this->AddSettingRow(page, s, QStringLiteral("droplet"), page_index);
            }
        }
        pages_->addWidget(page);
    }
    addSettingPage(QStringLiteral("Audio"), QStringLiteral("A U D I O"),
                   Settings::Category::Audio, QStringLiteral("droplet"));
    addSettingPage(QStringLiteral("System"), QStringLiteral("S Y S T E M"),
                   Settings::Category::Core, QStringLiteral("settings"));

    // Advanced: applets and the console-extra destinations.
    {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(QStringLiteral("Advanced"),
                                       QStringLiteral("A D V A N C E D"));
        AddActionRow(page, QStringLiteral("home"), tr("Handheld mode"), [this] {
            Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
            if (hooks.apply_live) {
                hooks.apply_live();
            }
        }, /*closes=*/false, page_index);
        AddActionRow(page, QStringLiteral("battery"), tr("Docked mode"), [this] {
            Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Docked);
            if (hooks.apply_live) {
                hooks.apply_live();
            }
        }, /*closes=*/false, page_index);
        for (Settings::BasicSetting* s : Settings::values.linkage.by_category.at(
                 Settings::Category::LibraryApplet)) {
            if (!IsSkippable(s, *translations)) {
                this->AddSettingRow(page, s, QStringLiteral("plus"), page_index);
            }
        }
        AddActionRow(page, QStringLiteral("star"), tr("All configuration..."), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(5);
            }
        }, /*closes=*/true, page_index);
        AddActionRow(page, QStringLiteral("folder"), tr("Filesystem paths..."), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(5);
            }
        }, /*closes=*/true, page_index);
        AddActionRow(page, QStringLiteral("gamepad"), tr("Controls..."), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(2);
            }
        }, /*closes=*/true, page_index);
        AddActionRow(page, QStringLiteral("timer"), tr("Hotkeys..."), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(0);
            }
        }, /*closes=*/true, page_index);
        pages_->addWidget(page);
    }

    // Game List - the OC game list settings live in the full dialog.
    {
        const int page_index = pages_->count();
        QWidget* page = MakeScrollPage(QStringLiteral("Game List"),
                                       QStringLiteral("G A M E   L I S T"));
        AddActionRow(page, QStringLiteral("list"), tr("Game List settings..."), [this] {
            if (hooks.open_configure) {
                hooks.open_configure(0);
            }
        }, /*closes=*/true, page_index);
        pages_->addWidget(page);
    }
}

QWidget* ConsoleSettings::MakeScrollPage(const QString& name, const QString& eyebrow) {
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("Ocarina2HomePanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("Ocarina2HomePanelRows"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto* rows = new QVBoxLayout(content);
    rows->setContentsMargins(2, 2, 2, 2);
    rows->setSpacing(6);
    rows->setAlignment(Qt::AlignTop);
    auto* label = new QLabel(content);
    label->setObjectName(QStringLiteral("Ocarina2PanelSection"));
    label->setText(eyebrow);
    label->setContentsMargins(4, 4, 4, 2);
    rows->addWidget(label);
    scroll->setWidget(content);
    Q_UNUSED(name);
    return scroll;
}

void ConsoleSettings::AddRow(QWidget* page, const QString& icon, const QString& label,
                             std::function<QString()> value, std::function<void(int)> on_cycle,
                             bool enabled, const QString& toolTip, int page_index) {
    auto* scroll = qobject_cast<QScrollArea*>(page);
    auto* content = scroll ? scroll->widget() : page;
    auto* rows = qobject_cast<QVBoxLayout*>(content->layout());
    auto* button = new QPushButton(content);
    button->setObjectName(QStringLiteral("Ocarina2PanelRow"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setEnabled(enabled);
    button->setIcon(Glyph(icon, 0xcfc9e3, 20));
    button->setIconSize(QSize(20, 20));
    if (!toolTip.isEmpty()) {
        button->setToolTip(toolTip);
    }
    rows->addWidget(button);

    Row row;
    row.button = button;
    row.text = [label, value]() -> QString {
        const QString current = value ? value() : QString();
        return current.isEmpty()
                   ? label
                   : label + QStringLiteral("    >    ") + current;
    };
    row.enabled = [enabled] { return enabled; };
    row.on_cycle = std::move(on_cycle);
    row.page_index = page_index;
    button->setText(row.text());
    connect(button, &QPushButton::clicked, this, [this, on_cycle = row.on_cycle](bool) {
        if (on_cycle) {
            on_cycle(+1);
        }
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
        RefreshRows();
    });
    rows_.push_back(std::move(row));
}

void ConsoleSettings::AddSettingRow(QWidget* page, Settings::BasicSetting* setting,
                                    const QString& icon, int page_index,
                                    bool unlock_when_emulation) {
    const auto found = translations->find(setting->Id());
    const QString label =
        found == translations->cend() ? QString::fromStdString(setting->GetLabel())
                                      : found->second.first;
    const QString tip =
        found == translations->cend() ? QString() : found->second.second;
    const bool locked_while_running = hooks.emulation_running && hooks.emulation_running() &&
                                      !setting->RuntimeModifiable();
    const bool enabled = unlock_when_emulation || !locked_while_running;
    AddRow(page, icon, label,
           [this, setting] { return ValueTextFor(setting, *combobox_translations); },
           [this, setting](int direction) { Cycle(setting, direction); },
           enabled, tip, page_index);
}

void ConsoleSettings::AddActionRow(QWidget* page, const QString& icon, const QString& label,
                                   std::function<void()> on_trigger, bool closes, int page_index) {
    auto* scroll = qobject_cast<QScrollArea*>(page);
    auto* content = scroll ? scroll->widget() : page;
    auto* rows = qobject_cast<QVBoxLayout*>(content->layout());
    auto* button = new QPushButton(content);
    button->setObjectName(QStringLiteral("Ocarina2PanelRow"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setIcon(Glyph(icon, 0xcfc9e3, 20));
    button->setIconSize(QSize(20, 20));
    button->setText(label);
    rows->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, on_trigger, closes](bool) {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
        if (on_trigger) {
            on_trigger();
        }
        if (closes) {
            accept();
        }
    });
    Row row;
    row.button = button;
    row.text = [label]() -> QString { return label; };
    row.enabled = [] { return true; };
    row.on_cycle = {};
    row.page_index = page_index;
    rows_.push_back(std::move(row));
}

void ConsoleSettings::Cycle(Settings::BasicSetting* setting, int direction) {
    if (setting->IsEnum()) {
        const auto found = combobox_translations->find(setting->EnumIndex());
        if (found != combobox_translations->cend() && !found->second.empty()) {
            const u32 current =
                static_cast<u32>(std::strtoul(setting->ToString().c_str(), nullptr, 0));
            const auto& keys = found->second;
            size_t idx = 0;
            for (size_t n = 0; n < keys.size(); ++n) {
                if (keys[n].first == current) {
                    idx = n;
                    break;
                }
            }
            const int step = direction >= 0 ? 1 : -1;
            const int count = static_cast<int>(keys.size());
            const int next = ((static_cast<int>(idx) + step) % count + count) % count;
            setting->LoadString(std::to_string(keys[next].first));
        } else {
            // Plain cycling when the combobox translations are absent.
            const std::string raw = setting->ToString();
            int value = raw.empty() ? 0 : std::stoi(raw, nullptr, 0);
            value = std::max(0, value + direction);
            setting->LoadString(std::to_string(value));
        }
    } else if (setting->TypeId() == QStringLiteral("bool")) {
        const std::string raw = setting->ToString();
        setting->LoadString(raw == "true" ? std::string{"false"} : std::string{"true"});
    } else if (setting->IsIntegral() || setting->IsFloatingPoint()) {
        const std::string raw = setting->ToString();
        double value = raw.empty() ? 0.0 : std::strtod(raw.c_str(), nullptr);
        const std::string min_s = setting->MinVal();
        const std::string max_s = setting->MaxVal();
        double next = value + direction;
        if (!min_s.empty()) {
            next = std::max(next, std::strtod(min_s.c_str(), nullptr));
        }
        if (!max_s.empty()) {
            next = std::min(next, std::strtod(max_s.c_str(), nullptr));
        }
        if (setting->IsIntegral()) {
            setting->LoadString(std::to_string(static_cast<long long>(std::lround(next))));
        } else {
            setting->LoadString(fmt::format("{:f}", next));
        }
    }
    if (hooks.apply_live) {
        hooks.apply_live();
    }
}

void ConsoleSettings::RefreshRows() {
    for (const auto& row : rows_) {
        if (row.button) {
            row.button->setText(row.text());
            row.button->setEnabled(row.enabled ? row.enabled() : true);
        }
    }
}

void ConsoleSettings::ShowPage(int page) {
    current_page_ = page;
    pages_->setCurrentIndex(page);
    SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
    // Focus the first enabled row of this page, like the Switch's settings screen does.
    for (const auto& row : rows_) {
        if (row.page_index == page && row.button && row.button->isEnabled()) {
            row.button->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void ConsoleSettings::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    const QRect target = geometry();
    setWindowOpacity(0.0);
    setGeometry(target.adjusted(target.width() / 16, target.height() / 16,
                                -target.width() / 16, -target.height() / 16));
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
}

void ConsoleSettings::HandleKey(Qt::Key key) {
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
    case Qt::Key_Right: {
        const int direction = key == Qt::Key_Right ? 1 : -1;
        if (auto* button = qobject_cast<QPushButton*>(focusWidget())) {
            for (const auto& row : rows_) {
                if (row.button == button && row.on_cycle && row.button->isEnabled()) {
                    row.on_cycle(direction);
                    SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
                    RefreshRows();
                    return;
                }
            }
        }
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
        if (auto* button = qobject_cast<QPushButton*>(focusWidget())) {
            button->click();
            return;
        }
        return;
    case Qt::Key_PageUp:
        ShowPage((current_page_ + pages_->count() - 1) % pages_->count());
        return;
    case Qt::Key_PageDown:
        ShowPage((current_page_ + 1) % pages_->count());
        return;
    case Qt::Key_Home:
    case Qt::Key_Escape:
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Back);
        reject();
        return;
    default:
        return;
    }
}

void ConsoleSettings::keyPressEvent(QKeyEvent* event) {
    HandleKey(static_cast<Qt::Key>(event->key()));
    QDialog::keyPressEvent(event);
}

bool ConsoleSettings::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            EnsureFocusVisible(widget);
        }
        return QDialog::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
        case Qt::Key_Escape:
        case Qt::Key_Home:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            HandleKey(static_cast<Qt::Key>(key->key()));
            return true;
        default:
            // Swallow unmodified leftovers instead of passing them to the window.
            if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ConsoleSettings::EnsureFocusVisible(QWidget* widget) {
    QWidget* parent = widget ? widget->parentWidget() : nullptr;
    while (parent) {
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
            // Qt 6: QScrollArea::ensureVisible takes a point in the content widget's
            // coordinates plus a margin radius; the "radius" is half the row plus
            // extra context, so a row always fits fully in view.
            const QPoint centre = widget->mapTo(scroll->widget(),
                                                QPoint(0, widget->height() / 2));
            scroll->ensureVisible(centre.x(), centre.y(), 0,
                                  widget->height() / 2 + 48);
            return;
        }
        parent = parent->parentWidget();
    }
}

void ConsoleSettings::mousePressEvent(QMouseEvent* event) {
    if (panel_ && !panel_->geometry().contains(event->pos())) {
        reject();
        return;
    }
    QDialog::mousePressEvent(event);
}

} // namespace Ocarina2Ui
