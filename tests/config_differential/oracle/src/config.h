// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace wf2_ht {

// The shipped default for each smoothing key. Named once here because two
// places need it and they must not drift: the Config members below, and the
// loader, where a value it refuses has to land on the default of the key it
// came from rather than on one shared by both.
inline constexpr float kDefaultLocalSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
inline constexpr float kDefaultRemoteSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

// Default hotkey bindings, as Windows virtual key codes. Written as codes
// rather than the VK_ macros because this header is included by translation
// units that do not pull in windows.h, and the INI publishes them as codes too.
inline constexpr int kDefaultToggleKey = 0x23;           // End
inline constexpr int kDefaultCycleModeKey = 0x21;        // Page Up
inline constexpr int kDefaultChordToggleKey = 0x59;      // Y, as Ctrl+Shift+Y
inline constexpr int kDefaultChordCycleModeKey = 0x47;   // G, as Ctrl+Shift+G

struct Config {
    // Held as the socket's own type so an out-of-range INI value cannot reach
    // UdpReceiver::Start by silently truncating to a wrong 16-bit port.
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // Virtual key codes. Every action has a nav-cluster key and a
    // Ctrl+Shift+<key> chord, and both fire it - the chord is there for
    // keyboards with no nav cluster.
    int toggle_key = kDefaultToggleKey;
    int cycle_mode_key = kDefaultCycleModeKey;
    int chord_toggle_key = kDefaultChordToggleKey;
    int chord_cycle_mode_key = kDefaultChordCycleModeKey;

    // No sensitivity, deadzone, response curve or axis inversion lives here,
    // for rotation or for position: the tracker owns pose shaping, so the pose
    // is consumed at 1:1 and one tracker profile behaves the same way in every
    // game. The protocol-to-engine sign conversion the camera does need is a
    // fixed part of the boundary in camera_transform.cpp, not a setting.

    // Smoothing is chosen per connection from the packet's source address, and
    // both values cover rotation and position alike. A tracker running on this
    // machine is already steady, so local_smoothing is 0.0 and nothing floors
    // it; a phone on WiFi jitters over the network, which is what
    // remote_smoothing is for.
    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    bool position_enabled = true;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;
};

// Reads HeadTracking.ini from `exe_dir` over `out`. Keys that are absent, or
// whose value the boundary checks in config_sanitize.h reject, leave the
// corresponding member of `out` at whatever it already held - so passing a
// default-constructed Config yields the shipped defaults.
void LoadConfig(const std::string& exe_dir, Config& out);

// Writes the documented default HeadTracking.ini into `exe_dir`, unless one is
// already there. Never overwrites a user's file.
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

}  // namespace wf2_ht
