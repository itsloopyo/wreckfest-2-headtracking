// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

namespace wf2_ht {

// The name this keyboard layout gives a virtual key code, for the one log line
// that tells the user which keys their config ended up on. Falls back to the
// code itself ("0x23") for a key the layout has no name for, so the line can
// never come out with a blank where a key should be.
std::string HotkeyName(int virtual_key);

}  // namespace wf2_ht
