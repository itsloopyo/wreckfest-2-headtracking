// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/input/key_bindings.h"

namespace wf2_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for wreckfest-2.
constexpr const char* kDisplayName = "Wreckfest 2";

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    legacy::Config read;
    const bool present = legacy::LoadConfig(input.ansi_path, read);

    // The reader keeps the port inside 1024-65535, each smoothing value finite
    // and inside 0-1 and each limit finite and inside 0-0.5, so all of them
    // carry over as they are. v0.1.0 had no sensitivity, inversion or reticle
    // setting, so nothing is dropped.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // LimitY set both vertical bounds.
    out.position_limit_x = read.limit_x;
    out.position_limit_y = read.limit_y;
    out.position_limit_y_down = read.limit_y;
    out.position_limit_z = read.limit_z;
    out.position_limit_z_back = read.limit_z_back;

    // Each action had a key that fired with Ctrl and Shift not both held and a
    // chord key that fired only while both were. The reader keeps every code a
    // bindable key from 0x01 to 0xFE, so the pair is always a list of two.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, read.toggle_key}, {kChord, read.chord_toggle_key}});
    out.cycle_tracking_mode_key =
        FormatKeyBindings({{KeyModifiers::kNone, read.cycle_mode_key}, {kChord, read.chord_cycle_mode_key}});

    return present ? cfg::ImportResult::Imported({}) : cfg::ImportResult::Absent({});
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::position_limit_x)
        .Concept<Concept::PositionLimitY>(&Config::position_limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::position_limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::position_limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::position_limit_z_back)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key);
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::filesystem::path& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = (folder / kIniName).wstring();
    options.table = Table();
    options.import = Import();
    options.legacy_path = (folder / kLegacyIniName).wstring();
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::filesystem::path& folder, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(folder, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("[config] %s", line.c_str());
    if (!result.reason.empty()) Log::Line("[config] %s", result.reason.c_str());
    Log::Line("[config] %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

cameraunlock::PositionSettings ToPositionSettings(const Config& config) {
    cameraunlock::PositionSettings position;
    position.limit_x = config.position_limit_x;
    position.limit_y = config.position_limit_y;
    position.limit_y_down = config.position_limit_y_down;
    position.limit_z = config.position_limit_z;
    position.limit_z_back = config.position_limit_z_back;
    position.local_smoothing = config.local_smoothing;
    position.remote_smoothing = config.remote_smoothing;
    return position;
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    const cfg::ConfigSaveResult result = g_owner->Save([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("[config] [General] RotationEnabled and [Position] PositionEnabled %s: %s",
                  cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("[config] %s", line.c_str());
}

}  // namespace wf2_ht::config
