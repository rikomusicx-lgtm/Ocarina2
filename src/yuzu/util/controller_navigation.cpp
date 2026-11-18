// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings_input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "yuzu/util/controller_navigation.h"

#include <algorithm>

ControllerNavigation::ControllerNavigation(Core::HID::HIDCore& hid_core, QWidget* parent) {
    player1_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
    handheld_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    repeat_timer = new QTimer(this);
    repeat_timer->setInterval(40);  // 25 Hz while a direction is held
    connect(repeat_timer, &QTimer::timeout, this, &ControllerNavigation::OnRepeatTick);
    LoadController();
}

ControllerNavigation::~ControllerNavigation() {
    UnloadController();
}

void ControllerNavigation::UnloadController() {
    if (is_controller_set) {
        player1_controller->DeleteCallback(player1_callback_key);
        handheld_controller->DeleteCallback(handheld_callback_key);
        is_controller_set = false;
    }
}

void ControllerNavigation::LoadController() {
    if (is_controller_set) {
        return;
    }
    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { ControllerUpdateEvent(type); },
        .is_npad_service = false,
    };
    player1_callback_key = player1_controller->SetCallback(engine_callback);
    handheld_callback_key = handheld_controller->SetCallback(engine_callback);
    is_controller_set = true;
}

void ControllerNavigation::TriggerButton(Settings::NativeButton::Values native_button,
                                         Qt::Key key) {
    if (button_values[native_button].value && !button_values[native_button].locked) {
        emit TriggerKeyboardEvent(key);
    }
}

void ControllerNavigation::ControllerUpdateEvent(Core::HID::ControllerTriggerType type) {
    std::scoped_lock lock{mutex};
    if (!Settings::values.controller_navigation) {
        return;
    }
    if (type == Core::HID::ControllerTriggerType::Button) {
        ControllerUpdateButton();
        return;
    }

    if (type == Core::HID::ControllerTriggerType::Stick) {
        ControllerUpdateStick();
        // A held stick behaves like a held d-pad: it feeds the auto-repeat timer.
        RefreshHeldDirections();
        return;
    }
}

void ControllerNavigation::ControllerUpdateButton() {
    const auto controller_type = player1_controller->GetNpadStyleIndex();
    const auto& player1_buttons = player1_controller->GetButtonsValues();
    const auto& handheld_buttons = handheld_controller->GetButtonsValues();

    for (std::size_t i = 0; i < player1_buttons.size(); ++i) {
        const bool button = player1_buttons[i].value || handheld_buttons[i].value;
        // Trigger only once
        button_values[i].locked = button == button_values[i].value;
        button_values[i].value = button;
    }

    switch (controller_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::GameCube:
        TriggerButton(Settings::NativeButton::A, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::B, Qt::Key_Escape);
        TriggerButton(Settings::NativeButton::DDown, Qt::Key_Down);
        TriggerButton(Settings::NativeButton::DLeft, Qt::Key_Left);
        TriggerButton(Settings::NativeButton::DRight, Qt::Key_Right);
        TriggerButton(Settings::NativeButton::DUp, Qt::Key_Up);
        TriggerButton(Settings::NativeButton::X, Qt::Key_Menu);
        TriggerButton(Settings::NativeButton::Plus, Qt::Key_Menu);
        TriggerButton(Settings::NativeButton::L, Qt::Key_PageUp);
        TriggerButton(Settings::NativeButton::R, Qt::Key_PageDown);
        TriggerButton(Settings::NativeButton::Home, Qt::Key_Home);
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        TriggerButton(Settings::NativeButton::DDown, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::DLeft, Qt::Key_Escape);
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        TriggerButton(Settings::NativeButton::X, Qt::Key_Enter);
        TriggerButton(Settings::NativeButton::A, Qt::Key_Escape);
        break;
    default:
        break;
    }
    RefreshHeldDirections();
}

void ControllerNavigation::RefreshHeldDirections() {
    std::vector<Qt::Key> now;
    const auto controller_type = player1_controller->GetNpadStyleIndex();
    const bool main_style = controller_type == Core::HID::NpadStyleIndex::Fullkey ||
                            controller_type == Core::HID::NpadStyleIndex::JoyconDual ||
                            controller_type == Core::HID::NpadStyleIndex::Handheld ||
                            controller_type == Core::HID::NpadStyleIndex::GameCube;
    if (main_style) {
        const auto held_button = [&](Settings::NativeButton::Values b) {
            return button_values[b].value;
        };
        const auto& s = stick_values[Settings::NativeAnalog::LStick];
        if (held_button(Settings::NativeButton::DDown) || s.down) {
            now.push_back(Qt::Key_Down);
        }
        if (held_button(Settings::NativeButton::DUp) || s.up) {
            now.push_back(Qt::Key_Up);
        }
        if (held_button(Settings::NativeButton::DLeft) || s.left) {
            now.push_back(Qt::Key_Left);
        }
        if (held_button(Settings::NativeButton::DRight) || s.right) {
            now.push_back(Qt::Key_Right);
        }
    }
    std::ranges::sort(now);
    now.erase(std::ranges::unique(now).begin(), now.end());

    std::vector<HeldKey> next;
    next.reserve(now.size());
    for (const Qt::Key key : now) {
        const auto it = std::find_if(held_keys_.begin(), held_keys_.end(),
                                     [&](const HeldKey& h) { return h.key == key; });
        next.push_back(it != held_keys_.end() ? *it : HeldKey{key, 0});
    }
    held_keys_.swap(next);
    if (held_keys_.empty()) {
        if (repeat_timer) {
            repeat_timer->stop();
        }
        return;
    }
    if (repeat_timer && !repeat_timer->isActive()) {
        repeat_timer->start();
    }
}

void ControllerNavigation::OnRepeatTick() {
    std::scoped_lock lock{mutex};
    for (auto& held : held_keys_) {
        ++held.ticks;
        if (held.ticks == kInitialDelayTicks + 1 ||
            (held.ticks > kInitialDelayTicks &&
             (held.ticks - kInitialDelayTicks) % kRepeatEveryTicks == 0)) {
            emit TriggerKeyboardEvent(held.key);
        }
    }
}

void ControllerNavigation::ControllerUpdateStick() {
    const auto controller_type = player1_controller->GetNpadStyleIndex();
    const auto& player1_sticks = player1_controller->GetSticksValues();
    const auto& handheld_sticks = handheld_controller->GetSticksValues();
    bool update = false;

    for (std::size_t i = 0; i < player1_sticks.size(); ++i) {
        const Common::Input::StickStatus stick{
            .left = player1_sticks[i].left || handheld_sticks[i].left,
            .right = player1_sticks[i].right || handheld_sticks[i].right,
            .up = player1_sticks[i].up || handheld_sticks[i].up,
            .down = player1_sticks[i].down || handheld_sticks[i].down,
        };
        // Trigger only once
        if (stick.down != stick_values[i].down || stick.left != stick_values[i].left ||
            stick.right != stick_values[i].right || stick.up != stick_values[i].up) {
            update = true;
        }
        stick_values[i] = stick;
    }

    if (!update) {
        return;
    }

    switch (controller_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::JoyconDual:
    case Core::HID::NpadStyleIndex::Handheld:
    case Core::HID::NpadStyleIndex::GameCube:
        if (stick_values[Settings::NativeAnalog::LStick].down) {
            emit TriggerKeyboardEvent(Qt::Key_Down);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].left) {
            emit TriggerKeyboardEvent(Qt::Key_Left);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].right) {
            emit TriggerKeyboardEvent(Qt::Key_Right);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].up) {
            emit TriggerKeyboardEvent(Qt::Key_Up);
            return;
        }
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        if (stick_values[Settings::NativeAnalog::LStick].left) {
            emit TriggerKeyboardEvent(Qt::Key_Down);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].up) {
            emit TriggerKeyboardEvent(Qt::Key_Left);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].down) {
            emit TriggerKeyboardEvent(Qt::Key_Right);
            return;
        }
        if (stick_values[Settings::NativeAnalog::LStick].right) {
            emit TriggerKeyboardEvent(Qt::Key_Up);
            return;
        }
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        if (stick_values[Settings::NativeAnalog::RStick].right) {
            emit TriggerKeyboardEvent(Qt::Key_Down);
            return;
        }
        if (stick_values[Settings::NativeAnalog::RStick].down) {
            emit TriggerKeyboardEvent(Qt::Key_Left);
            return;
        }
        if (stick_values[Settings::NativeAnalog::RStick].up) {
            emit TriggerKeyboardEvent(Qt::Key_Right);
            return;
        }
        if (stick_values[Settings::NativeAnalog::RStick].left) {
            emit TriggerKeyboardEvent(Qt::Key_Up);
            return;
        }
        break;
    default:
        break;
    }
}
