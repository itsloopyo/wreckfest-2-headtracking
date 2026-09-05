// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <string>
#include <thread>

#include "builds/build_registry.h"
#include "camera_hook.h"
#include "camera_transform.h"
#include "config.h"
#include "game_state.h"
#include "game_window.h"
#include "hotkeys.h"
#include "logging.h"
#include "render_diagnostics.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace wf2_ht {

namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
// Without IsRemoteConnection() on the receiver the session silently falls back
// to LocalSmoothing forever, with nothing at the call site to show it.
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;
cameraunlock::UdpReceiver g_receiver;
Session g_session(g_receiver);
cameraunlock::input::HotkeyPoller g_hotkeys;
cameraunlock::time::FrameClock g_frame_clock;

std::atomic<bool> g_tracking_enabled{false};
std::atomic<bool> g_active{false};

std::atomic<long long> g_frame_counter{0};

// Mode cycles pressed but not yet applied. CycleMode() resets the position
// interpolator and the position processor's smoothing state, which is a dozen
// plain floats the render thread reads inside the same session update - so the
// hotkey thread records the press here and the render thread performs the
// switch, between two updates rather than during one.
std::atomic<int> g_pending_mode_cycles{0};

// Whether this module is pinned against unloading - see PinModule. Recorded at
// load and reported from the bootstrap, which is the first point there is a log
// to report it into.
std::atomic<bool> g_pinned{false};

// ---------------------------------------------------------------------------
// Config to pipeline
// ---------------------------------------------------------------------------

// Translation only, from the INI-backed Config into the core pipeline's own
// settings types. Both arguments are explicit rather than reaching for the
// file statics, so this reads as - and can be reasoned about as - a mapping
// with no other reach into the mod's state.
void ApplyConfigToPipeline(const Config& config, Session& session) {
    // No sensitivity, deadzone, response curve or axis inversion is applied
    // here, and none is configurable: the tracker owns pose shaping, so the
    // pose is consumed at 1:1 scale and one tracker profile behaves the same
    // way in every game. The protocol-to-engine sign conversion this camera
    // does need is a fixed part of the boundary, in camera_transform.cpp.
    //
    // One pair of values for rotation and position alike. The session owns them
    // and recomposes them onto whatever position settings it is handed, so the
    // two calls below compose in either order and no settings rebuild can drop
    // them. Which of the two is in effect is decided per connection from the
    // receiver's source-address check, so nothing here picks one.
    session.SetLocalSmoothing(config.local_smoothing);
    session.SetRemoteSmoothing(config.remote_smoothing);

    session.SetPositionSettings(cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        false, false, false));

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void ToggleTracking() {
    const bool on = !g_tracking_enabled.load();
    g_tracking_enabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

void CycleTrackingMode() {
    g_pending_mode_cycles.fetch_add(1, std::memory_order_relaxed);
}

// On the render thread, between session updates. See g_pending_mode_cycles.
void ApplyPendingModeCycles() {
    for (int pending = g_pending_mode_cycles.exchange(0, std::memory_order_relaxed);
         pending > 0; --pending) {
        const char* name = "";
        switch (g_session.CycleMode()) {
            case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
            case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
            case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
        }
        Log::Line("[input] tracking mode: %s", name);
    }
}

std::array<HotkeyBinding, 2> Bindings(const Config& config) {
    return {{
        { "toggle tracking",     config.toggle_key,     config.chord_toggle_key,     ToggleTracking },
        { "cycle tracking mode", config.cycle_mode_key, config.chord_cycle_mode_key, CycleTrackingMode },
    }};
}

// ---------------------------------------------------------------------------
// Bootstrap
//
// Each step either leaves the mod ready or leaves it dormant with the reason in
// the log. Split out so the sequence below reads as the sequence.
// ---------------------------------------------------------------------------

bool OpenLogAndResolveGameDirectory(std::string& exe_dir) {
    // The core's resolver rather than a local copy of it: it grows its buffer
    // past MAX_PATH, so a game under a long install path resolves instead of
    // leaving the mod dormant, and it refuses a best-fit ANSI narrowing rather
    // than handing back the name of a different directory that happens to exist.
    const std::wstring exe_dir_wide = cameraunlock::os::HostExeDirectory();

    // Beside the game EXE, not in the process working directory: a launcher can
    // start the game from anywhere, and a bare relative name then drops the log
    // wherever that happens to be - or fails to create it at all - exactly when
    // a user is being asked to send one. An unresolved directory still needs
    // somewhere to say so before the mod goes dormant.
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"HeadTracking.log")
                                   : exe_dir_wide + L"\\HeadTracking.log");
    Log::Line("=== Wreckfest 2 Head Tracking ===");

    // The INI layer is ANSI-only (IniReader wraps GetPrivateProfile*A), so a
    // directory with no ANSI form is as unusable as one that would not resolve.
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::string& exe_dir) {
    WriteDefaultConfigIfMissing(exe_dir);
    LoadConfig(exe_dir, g_config);
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f position=%d",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.position_enabled ? 1 : 0);

    ApplyConfigToPipeline(g_config, g_session);
    g_tracking_enabled.store(g_config.enable_on_startup);
}

void StartReceiver() {
    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }
}

// Takes a reference on this module so an explicit FreeLibrary cannot unmap it.
// Three things outlive such a call: the detached bootstrap thread, the hotkey
// and receiver threads, and - the one that is fatal - an inline detour sitting
// in the game's camera update, which a thread can be executing at the moment
// the pages go away. An undo cannot be written for any of it, because it would
// have to run from DllMain under the loader lock, where joining those threads
// deadlocks. Refusing the unload removes the whole problem, and the bootstrap
// declines to start anything at all if this fails; the process exiting still
// reclaims everything.
//
// The address handed in has to be one inside this module, hence a function of
// our own rather than anything the caller passes.
bool PinModule() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&ApplyConfigToPipeline),
                              &self) != FALSE;
}

// Everything that decides whether the mod ends up active or dormant. Each
// early return leaves it dormant with the reason in the log.
//
// Returns false only for the one kind of dormancy that must also stop the
// window centring below: an unpinned module, which can still be unmapped out
// from under a thread sleeping in it. Every other dormant path leaves this
// module permanently loaded and is safe to keep waiting on.
bool BootstrapMod() {
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir)) return false;

    if (!g_pinned.load()) {
        // Dormant rather than degraded, and nothing below this line runs -
        // including the window centring, which sleeps inside this module for up
        // to thirty seconds. Everything past here is unsafe to leave in a module
        // that can still be unmapped, and the undo cannot be written: it would
        // have to run from DllMain under the loader lock, where joining the
        // threads it must join deadlocks instead of returning.
        Log::Line("[boot] this module could not be pinned against unloading - mod is "
                  "dormant, game runs vanilla.");
        return false;
    }

    // After the pin, because it installs a process-wide unhandled-exception
    // filter pointing into this module and core offers no way to take one back
    // out - on an unpinned module that filter would outlive the unload. Before
    // any hook, because this mod detours two engine functions and a stale offset
    // after a game patch faults inside our own detour; without this the user
    // sees an unattributed game crash and a log that stops mid-line.
    cameraunlock::diagnostics::InstallCrashHandler();

    if (builds::SelectProfile(GetModuleHandleW(nullptr)) != builds::ProfileSelection::Matched) {
        Log::Line("[boot] no usable build profile - mod is dormant, game runs vanilla.");
        return true;
    }
    if (!InitGameState()) {
        Log::Line("[boot] the gameplay gate could not be resolved - mod is dormant, "
                  "game runs vanilla.");
        return true;
    }

    LoadAndApplyConfig(exe_dir);
    StartReceiver();

    if (!InstallCameraHook()) {
        // Nothing will ever read the tracker now, so give the port back rather
        // than sitting on 4242 for the rest of the session and blocking
        // whatever else the user points their tracker at.
        g_receiver.Stop();
        Log::Line("[boot] the camera update could not be hooked - mod is inert.");
        return true;
    }

    const std::array<HotkeyBinding, 2> bindings = Bindings(g_config);
    AddBindings(g_hotkeys, bindings.data(), bindings.size());
    const bool hotkeys_started = g_hotkeys.Start();
    g_active.store(true, std::memory_order_release);

    if (!hotkeys_started) {
        Log::Line("[boot] ready, but the hotkey thread could not start - tracking cannot be "
                  "switched off or have its mode changed from the keyboard this session.");
        return true;
    }

    // Through %s, never as the format itself: the names come from the keyboard
    // layout, and a layout that names a key with a '%' would otherwise turn this
    // line into a format string reading arguments that were never passed.
    Log::Line("[boot] ready. %s", DescribeHotkeys(bindings.data(), bindings.size()).c_str());
    return true;
}

void Bootstrap() {
    // Last, because it blocks until the engine has a window up, and every
    // dormant path above still reaches it - the window is centred whether or
    // not this build is one the mod can hook. The exception is an unpinned
    // module, where a thread parked in this call is what an unload would unmap.
    if (BootstrapMod()) CenterGameWindowWhenItExists();
}

}  // namespace

bool ApplyTrackingToCameraTransform(float* transform) {
    // Acquire, to pair with the release store the bootstrap thread makes once
    // everything behind this flag is built: the config, the resolved engine
    // statics and the hook's cached offsets are all plain values written there
    // and read here.
    if (!g_active.load(std::memory_order_acquire)) return false;

    ApplyPendingModeCycles();

    // One camera reaches this per rendered frame on the build the mod has a
    // profile for, so this is the frame delta rather than a fraction of one.
    // Measured, not inferred from the single caller: across a race only the
    // render camera moved (46.87 degrees of yaw over the sample, against 0.05
    // on each of the view manager's three other embedded cameras).
    const float dt = g_frame_clock.Tick();

    // The pipeline advances whatever the gate says. Freezing it in the garage
    // and thawing it on the grid would compose a pose from wherever the head
    // was when the player last drove; advancing it means the first tracked
    // frame is composed from where the head is now, with nothing to jump from.
    if (g_session.Update(dt)) {
        diag::LogConnectionLocality(g_session.IsRemoteConnection(), g_config.local_smoothing,
                                    g_config.remote_smoothing);
    }
    diag::LogTrackerPresence(g_receiver.IsReceiving());

    const long long frame = g_frame_counter.fetch_add(1, std::memory_order_relaxed);

    HeadPose pose;
    const bool have_rotation = g_session.GetRotation(pose.yaw, pose.pitch, pose.roll);
    g_session.GetPositionOffset(pose.lean_x, pose.lean_y, pose.lean_z);

    diag::LogFirstFrames(frame, transform, have_rotation, pose);
    diag::LogFirstPoseReachingCamera(frame, have_rotation, pose);

    const ViewState view = ReadViewState();
    const GameplayReading gameplay = ReadGameplayState();
    const bool paused = IsGamePaused();
    const NetworkState network = ReadNetworkState();
    const bool tracking_enabled = g_tracking_enabled.load(std::memory_order_relaxed);
    const bool following = ShouldFollowHead(tracking_enabled, view, gameplay.running, paused);
    diag::LogGateChange(tracking_enabled, view, gameplay.running, paused, following);
    LogGameplayState(gameplay);
    LogNetworkState(network);

    // No tracker data, or the gate is shut: leave the engine's camera exactly
    // as it computed it.
    if (!have_rotation || !following) return false;

    diag::LogFirstComposedPose(pose);
    ApplyHeadPose(transform, pose);
    return true;
}

void Initialize() {
    // Before anything else, and before any thread exists to be caught by it.
    g_pinned.store(PinModule());

    // Detached: DllMain runs under the loader lock, so the bootstrap (which
    // opens a log, reads the INI and resolves engine statics) cannot run here.
    std::thread(Bootstrap).detach();
}

}  // namespace wf2_ht
