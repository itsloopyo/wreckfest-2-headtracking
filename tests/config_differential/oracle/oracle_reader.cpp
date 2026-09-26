// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// v0.1.0's reader and startup code, the newest published build (commit
// e1e72ba, tag object c315d5b).
//
// The reader is compiled from byte copies: src/config.cpp, src/config.h,
// src/config_sanitize.h and src/logging.h beside this file are v0.1.0's files
// (git show v0.1.0:src/<file>), which CMakeLists.txt pins by hash. They are
// included inside namespace wf2_oracle, after every header they include, so the
// published wf2_ht::Config and wf2_ht::LoadConfig become wf2_oracle::wf2_ht's
// and cannot collide with the mod's own. Every cameraunlock-core source they
// include holds the same bytes at v0.1.0's pin (992b910) and at this repo's
// (CMakeLists.txt pins those too).
//
// The startup code is transcribed from v0.1.0:src/headtracking_mod.cpp and
// v0.1.0:src/hotkeys.cpp, which hook the game and cannot be compiled into a
// test:
//
//   headtracking_mod.cpp lines 71-94    ApplyConfigToPipeline, recording what it
//                                       handed on
//   headtracking_mod.cpp lines 124-129  Bindings
//   hotkeys.cpp lines 12-20             AddBindings, recording each AddHotkey
//   headtracking_mod.cpp lines 173-174  the pipeline and the enabled flag at
//                                       startup

#include "oracle_reader.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/protocol/port_utils.h"

namespace wf2_oracle {
#include "src/config.cpp"
}  // namespace wf2_oracle

namespace wf2_oracle {

Published Read(const std::string& exe_dir) {
    wf2_ht::Config config;
    wf2_ht::LoadConfig(exe_dir, config);

    Published p;
    p.udp_port = config.udp_port;

    // ApplyConfigToPipeline. SetPositionSettings replaces the smoothing fields
    // of the settings it is handed with the session's pair, which the two calls
    // before it had just set to these same values.
    p.local_smoothing = config.local_smoothing;
    p.remote_smoothing = config.remote_smoothing;
    p.position = cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        false, false, false);
    p.position.local_smoothing = p.local_smoothing;
    p.position.remote_smoothing = p.remote_smoothing;
    p.mode = config.position_enabled ? kRotationAndPosition : kRotationOnly;

    p.tracking_enabled = config.enable_on_startup;

    // Bindings and AddBindings: each action's nav key NavGuarded, its chord key
    // ChordGuarded.
    const struct { int nav_key; int chord_key; Action action; } bindings[] = {
        { config.toggle_key,     config.chord_toggle_key,     kToggle },
        { config.cycle_mode_key, config.chord_cycle_mode_key, kCycleMode },
    };
    for (const auto& binding : bindings) {
        p.hotkeys.emplace_back(binding.action, binding.nav_key, 0u);
        p.hotkeys.emplace_back(binding.action, binding.chord_key, 3u);
    }
    return p;
}

void WriteFirstRunFile(const std::string& exe_dir) {
    wf2_ht::WriteDefaultConfigIfMissing(exe_dir);
}

}  // namespace wf2_oracle
