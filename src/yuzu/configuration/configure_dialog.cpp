// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QTimer>
#include <memory>
#include <algorithm>
#include <functional>
#include <QBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QToolButton>
#include <QGridLayout>
#include <QTabBar>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSpinBox>
#include "yuzu/configuration/configure_applets.h"
#include "yuzu/configuration/configure_audio.h"
#include "yuzu/configuration/configure_cpu.h"
#include "yuzu/configuration/configure_debug_tab.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_filesystem.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/configuration/configure_graphics.h"
#include "yuzu/configuration/configure_graphics_advanced.h"
#include "yuzu/configuration/configure_graphics_extensions.h"
#include "yuzu/configuration/configure_hotkeys.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_network.h"
#include "yuzu/configuration/configure_profile_manager.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/configuration/configure_ui.h"
#include "yuzu/configuration/configure_web.h"
#include "yuzu/hotkeys.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/sound.h"
#include "yuzu/ocarina2/controller_dialog.h"
#include <map>

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);
    if (Ocarina2Ui::Enabled()) {
        // The console settings screen: a Quick menu-style dim backdrop with a floating
        // "SETTINGS" card in the middle. LB/RB (PageUp/PageDown) cycles the categories, the
        // D-pad walks the rows, A adjusts, B backs out.
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        // Quick-menu style: a dim backdrop with a floating opaque card in the middle.
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_StyledBackground, true);
        if (const QWidget* top = parentWidget()) {
            setGeometry(top->geometry());  ///< fullscreen backdrop; the card floats inside it
        }
        ui->tabWidget->setObjectName(QStringLiteral("Ocarina2SettingsTabs"));
        ui->tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // The native tab bar can't be reached with a controller and clips long tab names; a
        // focusable chip row in the header takes its place (RebuildChips).
        ui->tabWidget->tabBar()->hide();
        // Hidden and zero-width, so the hidden list takes no room and the pages sit centred.
        ui->selectorList->setFixedWidth(0);
        ui->selectorList->setMaximumWidth(0);
        ui->selectorList->hide();  ///< the breadcrumb carries the category; LB/RB cycles them
        ui->tabWidget->setMaximumWidth(1100);
        // The console-style header: centered wordmark with a glow, then the breadcrumb.
        auto* header = new QWidget(this);
        header->setObjectName(QStringLiteral("Ocarina2SettingsHeader"));
        auto* header_layout = new QVBoxLayout(header);
        header_layout->setContentsMargins(0, 26, 0, 4);
        header_layout->setSpacing(4);
        header_layout->setAlignment(Qt::AlignHCenter);
        auto* title = new QLabel(header);
        title->setObjectName(QStringLiteral("Ocarina2SettingsTitle"));
        title->setText(QStringLiteral("S E T T I N G S"));
        title->setAlignment(Qt::AlignHCenter);
        auto* glow = new QGraphicsDropShadowEffect(title);
        glow->setBlurRadius(38);
        glow->setOffset(0, 0);
        glow->setColor(QColor(120, 226, 255, 150));
        title->setGraphicsEffect(glow);
        header_layout->addWidget(title, 0, Qt::AlignHCenter);

        // No arrow buttons here - LB/RB cycles the categories already.
        auto* crumb = new QLabel(header);
        crumb->setObjectName(QStringLiteral("Ocarina2SettingsCrumb"));
        crumb_label = crumb;
        crumb->setAlignment(Qt::AlignHCenter);
        header_layout->addWidget(crumb, 0, Qt::AlignHCenter);
        const auto set_crumb = [crumb](const QString& category) {
            crumb->setText(QStringLiteral("SETTINGS › ") + category.toUpper());
        };
        connect(ui->selectorList, &QListWidget::currentItemChanged, this,
                [set_crumb](QListWidgetItem*, QListWidgetItem* current) {
                    if (current) {
                        set_crumb(current->text());
                    }
                });
        if (auto* item = ui->selectorList->currentItem()) {
            set_crumb(item->text());
        } else {
            // The list is filled later in the ctor; grab the selection once it exists.
            QTimer::singleShot(0, this, [set_crumb, this] {
                if (auto* selected = ui->selectorList->currentItem()) {
                    set_crumb(selected->text());
                }
            });
        }
        auto* card = ui->card;
        card->setObjectName(QStringLiteral("Ocarina2SettingsCard"));
        auto* card_layout = ui->cardLayout;
        card_layout->insertWidget(0, header, 0, Qt::AlignHCenter);
        header->setMaximumWidth(840);
        // The footer row: center the note and the OK/Cancel pair.
        if (auto* h2 = findChild<QHBoxLayout*>(QStringLiteral("horizontalLayout_2"))) {
            h2->insertStretch(0, 1);
            h2->addStretch(1);
        }
        // The hidden selectorList is zero-width; the tab pane fills the card and the page
        // inside each tab centres itself with stretches (see SettingsPage).

        // The chip row: every visible page as a focusable pill, laid out in up to two rows.
        chip_row = new QWidget(card);
        chip_row->setObjectName(QStringLiteral("Ocarina2SettingsChips"));
        auto* chip_layout = new QGridLayout(chip_row);
        chip_layout->setContentsMargins(0, 2, 0, 2);
        chip_layout->setHorizontalSpacing(8);
        chip_layout->setVerticalSpacing(8);
        chip_layout->setAlignment(Qt::AlignHCenter);
        card_layout->insertWidget(1, chip_row, 0, Qt::AlignHCenter);
    }

    ui->tabWidget->addTab(applets_tab.get(), tr("Applets"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));
    ui->tabWidget->addTab(debug_tab_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(filesystem_tab.get(), tr("Filesystem"));
    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(graphics_advanced_tab.get(), tr("GraphicsAdvanced"));
    ui->tabWidget->addTab(graphics_extensions_tab.get(), tr("GraphicsExtra"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Hotkeys"));
    ui->tabWidget->addTab(input_tab.get(), tr("Controls"));
    ui->tabWidget->addTab(profile_tab.get(), tr("Profiles"));
    ui->tabWidget->addTab(network_tab.get(), tr("Network"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(ui_tab.get(), tr("Game List"));
    ui->tabWidget->addTab(web_tab.get(), tr("Web"));

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();
    PopulateSelectionList();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != -1) {
            debug_tab_tab->SetCurrentIndex(0);
        }
    });
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int) { SyncChips(); });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigureDialog::HandleApplyButtonClicked);
    }

    adjustSize();
    ui->selectorList->setCurrentRow(0);
    if (Ocarina2Ui::Enabled()) {
        // After the first group is shown: its pages sit in scroll areas by then, so the window
        // can take a normal size.
        setWindowTitle(tr("Settings"));
        ui->label->setText(tr("Some settings can't change while a game runs."));
        ui->label->setVisible(system.IsPoweredOn());
        ui->label->setContentsMargins(26, 10, 26, 0);
        ui->selectorList->setFixedWidth(220);
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
        setMinimumSize(880, 560);
        // A proper console-menu palette so disabled text stays readable on the dark purple.
        QPalette pal;
        pal.setColor(QPalette::Window, QColor(0x1d, 0x1a, 0x2e));
        pal.setColor(QPalette::WindowText, QColor(0xe6, 0xe3, 0xef));
        pal.setColor(QPalette::Base, QColor(0x16, 0x14, 0x24));
        pal.setColor(QPalette::Text, QColor(0xe6, 0xe3, 0xef));
        pal.setColor(QPalette::Button, QColor(0x22, 0x1f, 0x33));
        pal.setColor(QPalette::ButtonText, QColor(0xe6, 0xe3, 0xef));
        pal.setColor(QPalette::BrightText, QColor(0xff, 0x8a, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0xa5, 0xa2, 0xb6));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa5, 0xa2, 0xb6));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xa5, 0xa2, 0xb6));
        pal.setColor(QPalette::Highlight, QColor(0x3f, 0xd8, 0xff));
        pal.setColor(QPalette::HighlightedText, QColor(0x0b, 0x15, 0x20));
        setPalette(pal);
        if (const QWidget* top = parentWidget()) {
            setGeometry(top->geometry());
        }

        // OoT's own values (Configure Game) beat these, the usual "my settings didn't apply".
        if (const QStringList overrides = Ocarina2Ui::OcarinaGameOverrides();
            !overrides.isEmpty()) {
            auto* const banner = new QWidget(this);
            auto* const row = new QHBoxLayout(banner);
            row->setContentsMargins(26, 8, 26, 8);
            auto* const text = new QLabel(
                tr("Ocarina of Time keeps its own value for %n of these settings, so changes "
                   "here don't reach it.",
                   nullptr, static_cast<int>(overrides.size())),
                banner);
            text->setWordWrap(true);
            text->setToolTip(overrides.join(QLatin1Char('\n')));
            auto* const use_global = new QPushButton(tr("Use these for OoT too"), banner);
            row->addWidget(text, 1);
            row->addWidget(use_global);
            if (auto* const card_layout = ui->cardLayout) {
                banner->setMaximumWidth(1100);
                card_layout->insertWidget(0, banner, 0, Qt::AlignHCenter);
            }
            connect(use_global, &QPushButton::clicked, this, [banner] {
                if (Ocarina2Ui::ClearOcarinaGameOverrides()) {
                    banner->hide();
                }
            });
        }
    }

    // Gamepad/keyboard drives the whole dialog like a console settings screen. Install the
    // filter after every child exists so nothing can focus-traverse out of it, and so the
    // banner/header widgets are covered too.
    {
        auto* ui_filter = new Ocarina2Ui::ControllerDialogFilter(this);
        controller_filter = ui_filter;
        ui_filter->page_cycle = [this](int delta) {
            const int count = ui->selectorList->count();
            if (count == 0) {
                return;
            }
            const int target = (ui->selectorList->currentRow() + delta + count) % count;
            ui->selectorList->setCurrentRow(target);
        };
        ui_filter->escape = [this] { reject(); };
        Ocarina2Ui::ControllerDialogFilter::InstallRecursive(this, ui_filter);
    }

    // Land on the first real control of the visible page, like a console does.
    FocusFirstControl();
}

void ConfigureDialog::FocusFirstControl() {
    QWidget* page = ui->tabWidget->currentWidget();
    if (!page || !Ocarina2Ui::Enabled()) {
        return;
    }
    // Ordered traversal of the visible page, depth first, for the first real control.
    std::function<QWidget*(QWidget*)> find_focusable;
    find_focusable = [&](QWidget* root) -> QWidget* {
        for (QObject* child : root->children()) {
            auto* widget = qobject_cast<QWidget*>(child);
            if (!widget || !widget->isVisible() || !widget->isEnabled()) {
                continue;
            }
            if ((widget->focusPolicy() & Qt::TabFocus) != 0 &&
                !widget->inherits("QScrollArea") && !widget->inherits("QFrame")) {
                return widget;
            }
            if (auto* found = find_focusable(widget)) {
                return found;
            }
        }
        return nullptr;
    };
    if (auto* first = find_focusable(page)) {
        first->setFocus(Qt::OtherFocusReason);
    }
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::OcarinaSelectGroup(int row) {
    const int count = ui->selectorList->count();
    if (count <= 0) {
        return;
    }
    const int target = std::clamp(row, 0, count - 1);
    ui->selectorList->setCurrentRow(target);
    FocusFirstControl();
}

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_row = ui->selectorList->currentRow();
    const int old_index = ui->tabWidget->currentIndex();

    ui->retranslateUi(this);

    PopulateSelectionList();
    ui->selectorList->setCurrentRow(old_row);

    UpdateVisibleTabs();
    ui->tabWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::pair<QString, QList<QWidget*>>, 6> items{
        {{tr("General"),
          {general_tab.get(), hotkeys_tab.get(), ui_tab.get(), web_tab.get(), debug_tab_tab.get()}},
         {tr("System"),
          {system_tab.get(), profile_tab.get(), network_tab.get(), filesystem_tab.get(),
           applets_tab.get()}},
         {tr("CPU"), {cpu_tab.get()}},
         {tr("Graphics"),
          {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()}},
         {tr("Audio"), {audio_tab.get()}},
         {tr("Controls"), input_tab->GetSubTabs()}},
    };

    // Ocarina2: everyday settings first, everything a player rarely needs under Advanced.
    const std::array<std::pair<QString, QList<QWidget*>>, 6> ocarina2_items{
        {{tr("General"), {general_tab.get(), ui_tab.get(), hotkeys_tab.get()}},
         {tr("Graphics"),
          {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()}},
         {tr("Controls"), input_tab->GetSubTabs()},
         {tr("Audio"), {audio_tab.get()}},
         {tr("System"), {system_tab.get(), profile_tab.get()}},
         {tr("Advanced"),
          {cpu_tab.get(), network_tab.get(), filesystem_tab.get(), applets_tab.get(),
           web_tab.get(), debug_tab_tab.get()}}},
    };
    if (Ocarina2Ui::Enabled()) {
        ui_tab->setAccessibleName(tr("Interface"));
        hotkeys_tab->setAccessibleName(tr("Shortcuts"));
        profile_tab->setAccessibleName(tr("Users"));
    }

    const auto icon_for = [](const QString& name) {
        return Ocarina2Ui::Enabled() ? Ocarina2Ui::Icon(name, 20) : QIcon();
    };

    [[maybe_unused]] const QSignalBlocker blocker(ui->selectorList);

    ui->selectorList->clear();
    ui->selectorList->setIconSize(QSize(22, 22));
    for (const auto& entry : Ocarina2Ui::Enabled() ? ocarina2_items : items) {
        auto* const item = new QListWidgetItem(entry.first);
        if (Ocarina2Ui::Enabled()) {
            // Switch 2-ish glyph per category so the settings rail reads at a glance.
            static const std::map<QString, QString> icons{
                {QStringLiteral("General"), QStringLiteral("triforce")},
                {QStringLiteral("Graphics"), QStringLiteral("star")},
                {QStringLiteral("Controls"), QStringLiteral("gamepad")},
                {QStringLiteral("Audio"), QStringLiteral("pause")},
                {QStringLiteral("System"), QStringLiteral("settings")},
                {QStringLiteral("Advanced"), QStringLiteral("menu")},
                {QStringLiteral("CPU"), QStringLiteral("restart")},
            };
            const auto found = icons.find(entry.first);
            item->setIcon(icon_for(found != icons.end() ? found->second : QStringLiteral("settings")));
        }
        item->setData(Qt::UserRole, QVariant::fromValue(entry.second));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    //  Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    [[maybe_unused]] const QSignalBlocker blocker(ui->tabWidget);

    ui->tabWidget->clear();

    const auto tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (auto* const tab : tabs) {
        LOG_DEBUG(Frontend, "{}", tab->accessibleName().toStdString());
        ui->tabWidget->addTab(Ocarina2Ui::SettingsPage(tab), tab->accessibleName());
    }

    // The console layout: the breadcrumb names the visible group, and the chips above the
    // pages mirror the pages the controller can reach.
    if (crumb_label) {
        crumb_label->setText(QStringLiteral("SETTINGS › ") + items[0]->text().toUpper());
    }
    RebuildChips();
}

void ConfigureDialog::RebuildChips() {
    if (!chip_row) {
        return;
    }
    auto* grid = qobject_cast<QGridLayout*>(chip_row->layout());
    if (!grid) {
        return;
    }
    // Track whether a chip holds focus, so LB/RB can't strand it on a dead widget.
    const QWidget* old_focus = QApplication::focusWidget();
    bool chip_had_focus = false;
    while (QLayoutItem* item = grid->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            chip_had_focus = chip_had_focus || widget == old_focus;
            widget->deleteLater();
        }
        delete item;
    }

    constexpr int kColumns = 5;
    const int count = ui->tabWidget->count();
    for (int i = 0; i < count; ++i) {
        auto* chip = new QToolButton(chip_row);
        chip->setObjectName(QStringLiteral("Ocarina2SettingsChip"));
        chip->setText(ui->tabWidget->tabText(i).toUpper());
        chip->setCheckable(true);
        chip->setAutoExclusive(true);
        chip->setChecked(i == ui->tabWidget->currentIndex());
        chip->setFocusPolicy(Qt::StrongFocus);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("chipIndex", i);
        connect(chip, &QToolButton::clicked, this, [this, i] { ui->tabWidget->setCurrentIndex(i); });
        if (controller_filter) {
            chip->installEventFilter(controller_filter);
        }
        grid->addWidget(chip, i / kColumns, i % kColumns, Qt::AlignCenter);
    }
    chip_row->setVisible(count > 1);

    if (chip_had_focus && count > 0) {
        const int current = std::clamp(ui->tabWidget->currentIndex(), 0, count - 1);
        for (QToolButton* candidate : chip_row->findChildren<QToolButton*>()) {
            if (candidate->property("chipIndex").toInt() == current) {
                candidate->setFocus(Qt::OtherFocusReason);
                break;
            }
        }
    }
}

void ConfigureDialog::SyncChips() {
    if (!chip_row) {
        return;
    }
    const int current = ui->tabWidget->currentIndex();
    const auto chips = chip_row->findChildren<QToolButton*>();
    for (QToolButton* chip : chips) {
        chip->setChecked(chip->property("chipIndex").toInt() == current);
    }
}
