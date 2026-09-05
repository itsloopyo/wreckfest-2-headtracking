// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <string>

#include "cameraunlock/input/hotkey_poller.h"

namespace wf2_ht {

// Every action is reachable two ways: its nav-cluster key, and the
// Ctrl+Shift+<letter> chord for keyboards without a nav cluster. Pairing them
// in one row is what keeps the two lists from drifting apart - a new action
// cannot pick up a nav key and silently miss its chord, and the line that tells
// the user which keys they ended up on is built from these same rows rather
// than from a second hand-written copy of the pairing.
struct HotkeyBinding {
    const char* action;
    int nav_key;
    int chord_key;
    void (*handler)();
};

// Registers each row twice on `poller`: the nav key, guarded so it does not
// also fire while the chord modifier is held, and the chord itself. Does not
// start the poller - that is the caller's, so configuring the bindings and
// starting the thread that watches them stay separable.
void AddBindings(cameraunlock::input::HotkeyPoller& poller, const HotkeyBinding* bindings,
                 std::size_t count);

// The keys each action ended up on, named for this keyboard layout:
// "End/Ctrl+Shift+Y toggle tracking, PgUp/Ctrl+Shift+G cycle tracking mode."
// Pure, so the one line a user is asked to read back out of the log can be
// checked without a keyboard or a running game. Empty for no bindings.
std::string DescribeHotkeys(const HotkeyBinding* bindings, std::size_t count);

}  // namespace wf2_ht
