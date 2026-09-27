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

namespace wf2_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for wreckfest-2.
constexpr const char* kDisplayName = "Wreckfest 2";

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    legacy::Config read;
    const bool present = legacy::LoadConfig(input.ansi_path, read);
    std::vector<cfg::DroppedValue> dropped;

    // The reader keeps the port inside 1024-65535, each smoothing value finite
    // and inside 0-1 and each limit finite and inside 0-0.5, so all of them
    // carry over as they are. v0.1.0 had no sensitivity, inversion or reticle
    // setting.
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
    // chord key that fired only while both were.
    const auto bindings = [&](int key, const char* key_name, int chord, const char* chord_name) {
        std::string list = cfg::LegacyVirtualKeyToBindings(key, "Hotkeys", key_name, dropped);
        const std::string chord_key = cfg::LegacyVirtualKeyToBindings(chord, "Hotkeys", chord_name, dropped);
        if (!chord_key.empty()) list += (list.empty() ? "Ctrl+Shift+" : ", Ctrl+Shift+") + chord_key;
        return list;
    };
    out.toggle_key = bindings(read.toggle_key, "ToggleKey", read.chord_toggle_key, "ChordToggleKey");
    out.cycle_tracking_mode_key =
        bindings(read.cycle_mode_key, "CycleModeKey", read.chord_cycle_mode_key, "ChordCycleModeKey");

    // A setting the player never changed from what v0.1.0 wrote follows
    // Defaults.ini. LimitY stood for both vertical bounds, and each hotkey for
    // its key and its chord key together.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, read.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, read.enable_on_startup, shipped.enable_on_startup);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, read.limit_x, shipped.limit_x);
    follows.Setting(Concept::PositionLimitY, read.limit_y, shipped.limit_y);
    follows.Setting(Concept::PositionLimitYDown, read.limit_y, shipped.limit_y);
    follows.Setting(Concept::PositionLimitZ, read.limit_z, shipped.limit_z);
    follows.Setting(Concept::PositionLimitZBack, read.limit_z_back, shipped.limit_z_back);
    follows.Setting(Concept::ToggleKey,
                    read.toggle_key == shipped.toggle_key && read.chord_toggle_key == shipped.chord_toggle_key);
    follows.Setting(Concept::CycleTrackingModeKey, read.cycle_mode_key == shipped.cycle_mode_key &&
                                                       read.chord_cycle_mode_key == shipped.chord_cycle_mode_key);

    return present ? cfg::ImportResult::Imported(std::move(dropped), {}, follows.Concepts())
                   : cfg::ImportResult::Absent(std::move(dropped), {}, follows.Concepts());
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
