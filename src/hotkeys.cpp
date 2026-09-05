// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include "hotkey_names.h"

#include "cameraunlock/input/chord_hotkeys.h"

namespace wf2_ht {

void AddBindings(cameraunlock::input::HotkeyPoller& poller, const HotkeyBinding* bindings,
                 std::size_t count) {
    using namespace cameraunlock::input;

    for (std::size_t i = 0; i < count; ++i) {
        poller.AddHotkey(bindings[i].nav_key, NavGuarded(bindings[i].handler));
        poller.AddHotkey(bindings[i].chord_key, ChordGuarded(bindings[i].handler));
    }
}

std::string DescribeHotkeys(const HotkeyBinding* bindings, std::size_t count) {
    std::string described;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) described += ", ";
        described += HotkeyName(bindings[i].nav_key) + "/Ctrl+Shift+"
                   + HotkeyName(bindings[i].chord_key) + " " + bindings[i].action;
    }
    if (!described.empty()) described += '.';
    return described;
}

}  // namespace wf2_ht
