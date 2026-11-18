// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QDialog>
#include "configuration/shared_widget.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/util/vk.h"
#include "yuzu/configuration/configuration_shared.h"

namespace Core {
class System;
}

class ConfigureApplets;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureDebugTab;
class ConfigureFilesystem;
class ConfigureGeneral;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureGraphicsExtensions;
class ConfigureHotkeys;
class ConfigureInput;
class ConfigureProfileManager;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureUi;
class ConfigureWeb;

class HotkeyRegistry;
class QLabel;
class QWidget;

namespace InputCommon {
class InputSubsystem;
}

namespace Ui {
class ConfigureDialog;
}

class ConfigureDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                             InputCommon::InputSubsystem* input_subsystem,
                             std::vector<VkDeviceInfo::Record>& vk_device_records,
                             Core::System& system_, bool enable_web_config = true);
    ~ConfigureDialog() override;

    void ApplyConfiguration();
    /// Selects one of the Ocarina2 selector groups (0=General, 1=Graphics, 2=Controls,
    /// 3=Audio, 4=System, 5=Advanced) before the dialog shows.
    void OcarinaSelectGroup(int row);

private slots:
    void OnLanguageChanged(const QString& locale);

signals:
    void LanguageChanged(const QString& locale);
    void ExternalContentDirsChanged();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void HandleApplyButtonClicked();

    void SetConfiguration();
    void UpdateVisibleTabs();
    void PopulateSelectionList();
    /// The Ocarina2 top row of page chips (replaces the native tab bar, which the controller
    /// can't reach): one focusable chip per visible page.
    void RebuildChips();
    void SyncChips();
    /// Focus the first focusable control on the visible page, like a console does.
    void FocusFirstControl();

    std::unique_ptr<Ui::ConfigureDialog> ui;
    /// The breadcrumb label in the header, so the selection list can keep it honest.
    QLabel* crumb_label = nullptr;
    /// The chip row's container; chips are rebuilt whenever the visible pages change.
    QWidget* chip_row = nullptr;
    /// The controller navigation filter, so chips created after the ctor still receive it.
    QObject* controller_filter = nullptr;
    HotkeyRegistry& registry;

    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::vector<ConfigurationShared::Tab*> tab_group;

    std::unique_ptr<ConfigureApplets> applets_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureDebugTab> debug_tab_tab;
    std::unique_ptr<ConfigureFilesystem> filesystem_tab;
    std::unique_ptr<ConfigureGeneral> general_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureUi> ui_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureHotkeys> hotkeys_tab;
    std::unique_ptr<ConfigureInput> input_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureProfileManager> profile_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureWeb> web_tab;
};
