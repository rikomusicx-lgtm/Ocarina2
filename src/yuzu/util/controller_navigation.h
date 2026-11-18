// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QKeyEvent>
#include <QObject>

#include "common/input.h"
#include "common/settings_input.h"

#include <QTimer>
#include <vector>

namespace Core::HID {
using ButtonValues = std::array<Common::Input::ButtonStatus, Settings::NativeButton::NumButtons>;
using SticksValues = std::array<Common::Input::StickStatus, Settings::NativeAnalog::NumAnalogs>;
enum class ControllerTriggerType;
class EmulatedController;
class HIDCore;
} // namespace Core::HID

class ControllerNavigation : public QObject {
    Q_OBJECT

public:
    explicit ControllerNavigation(Core::HID::HIDCore& hid_core, QWidget* parent = nullptr);
    ~ControllerNavigation();

    /// Disables events from the emulated controller
    void UnloadController();
    /// Re-enables events after UnloadController (callbacks were deleted there).
    void LoadController();

signals:
    void TriggerKeyboardEvent(Qt::Key key);

private:
    void TriggerButton(Settings::NativeButton::Values native_button, Qt::Key key);
    void ControllerUpdateEvent(Core::HID::ControllerTriggerType type);

    void ControllerUpdateButton();

    void ControllerUpdateStick();

    /// Recomputes which direction keys are currently held and keeps the auto-repeat timer
    /// running while at least one is (a held stick scrolls the console menus constantly,
    /// not one step per press).
    void RefreshHeldDirections();
    void OnRepeatTick();

    Core::HID::ButtonValues button_values{};
    Core::HID::SticksValues stick_values{};

    struct HeldKey {
        Qt::Key key;
        int ticks;  ///< 40 ms ticks since the key went down
    };
    std::vector<HeldKey> held_keys_;
    QTimer* repeat_timer = nullptr;
    static constexpr int kInitialDelayTicks = 7;   ///< ~280 ms before the first repeat
    static constexpr int kRepeatEveryTicks = 2;    ///< a repeat every ~80 ms while held

    int player1_callback_key{};
    int handheld_callback_key{};
    bool is_controller_set{};
    mutable std::mutex mutex;
    Core::HID::EmulatedController* player1_controller;
    Core::HID::EmulatedController* handheld_controller;
};
