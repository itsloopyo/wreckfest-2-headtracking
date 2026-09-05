// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <array>
#include <cstdint>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

namespace wf2_ht {

namespace {

// The profile's numbers, resolved to running addresses once at init so the
// render path never looks a profile up. Grouped by the chain each belongs to,
// because a chain is what faults, what gets logged and what a patch moves - and
// the active profile cannot change while the mod is running.
//
// Every address below is of the module-static POINTER, not of what it points
// at: the engine fills those long after this mod loads and replaces them across
// a session, so every read starts from the static.

struct ViewChain {
    std::uintptr_t manager_static = 0;
    unsigned active_controller = 0;
};

// The static is zero when the active profile has no pause offsets, which is the
// one case IsGamePaused answers without reading.
struct PauseChain {
    std::uintptr_t state_static = 0;
    unsigned object = 0;
    unsigned paused = 0;
};

// The gameplay gate: the static behind the romu::GameplayState singleton, the
// three fields the gate reads out of it, and the two phase values the profile
// pins as the ones head tracking follows through.
struct GameplayChain {
    std::uintptr_t state_static = 0;
    unsigned phase_offset = 0;
    unsigned countdown_phase = 0;
    unsigned racing_phase = 0;
    unsigned countdown_ms_offset = 0;
};

// The two network singletons and the state field inside each, plus the
// inclusive range of client states that count as a live session.
struct NetChain {
    std::uintptr_t client_static = 0;
    unsigned client_state = 0;
    unsigned client_session_lo = 0;
    unsigned client_session_hi = 0;
    std::uintptr_t server_static = 0;
    unsigned server_state = 0;
};

ViewChain g_view;
PauseChain g_pause;
GameplayChain g_gameplay;
NetChain g_net;

// One camera controller class the view manager can make active, with its vtable
// resolved to a running address. Identity by vtable pointer rather than by RTTI
// name: the comparison is what the render path can afford, and a vtable address
// is exactly as specific as the class.
struct ControllerClass {
    std::uintptr_t vtable;
    bool is_gameplay;
    const char* name;
};

// GarageCamera and CarPaintCamera are the menus, the garage and the livery
// editor. The rest are the race: the car camera the player drives with, the
// trackside and scripted cameras replays cut to, the free camera, and photo
// mode. A controller matching none of them is not assumed to be either.
constexpr unsigned kControllerClassCount = 7;
std::array<ControllerClass, kControllerClassCount> g_controllers{};

// How far into the view manager the unknown-controller diagnostic looks for a
// slot that does hold one. The controllers themselves start at +0x40, so the
// pointers to them live below that, and the scan runs at most once on a path
// that has already given up.
constexpr unsigned kViewManagerScanBytes = 0x100;

// A chain that faults says so once and then stays quiet. These reads are on the
// render path and a chain that faults tends to fault every frame, so a line per
// fault is a log nobody can read - while saying nothing at all leaves the one
// failure that silently disables the gate with no trace in the log. One latch
// per chain so a noisy chain cannot hide a quieter one. Only the render thread
// touches them.
struct ChainFault {
    const char* name;
    // What the mod does while this chain is down. The three chains answer a
    // fault in different directions, so one shared sentence would be wrong for
    // two of them - and this line is the whole of what a bug report has to go
    // on.
    const char* consequence;
    bool logged;
};

ChainFault g_view_chain{"view manager",
                        "the gameplay gate reads unknown and head tracking stays off", false};
ChainFault g_pause_chain{"pause state",
                         "the gate cannot see the pause menu and head tracking keeps "
                         "following through it", false};
ChainFault g_gameplay_chain{"gameplay state",
                            "the gate sees no running gameplay and head tracking stays off",
                            false};
ChainFault g_net_chain{"network state",
                       "the log cannot say whether a race was online, which costs triage "
                       "rather than head tracking", false};

// Latched separately again, and for the same reason: a controller the profile
// does not recognise is not a faulting chain, it is a profile that has fallen
// behind the game.
bool g_logged_unknown_controller = false;

// Which controller class the gate last saw, so the log carries the class the
// view actually changed to rather than only the state it mapped to.
const char* g_last_controller_name = nullptr;

void NoteFault(ChainFault& chain) {
    if (chain.logged) return;
    chain.logged = true;
    Log::Line("[state] the %s pointer chain faulted - %s while it does.",
              chain.name, chain.consequence);
}

// Reads a pointer-sized field, treating a null or unreadable one as "no
// answer". Every engine read in this file is this shape.
//
// Only an UNREADABLE address is reported. A null one is not, and the two are
// different questions: an address that faults is a chain pointing at memory
// that is not there, while a null is what each of these singletons holds from
// process start until the engine builds it, which is most of a launch.
// Reporting nulls would put a fault line in every log and latch the chain,
// spending the one report a real fault would have produced later.
//
// The cost is worth stating rather than hiding: a patch that moves a field so
// it lands on a permanently-zero slot reads as "not built yet" forever and says
// nothing. Telling those apart needs to know which links hold an object from
// process start, and that has not been measured on this build for any chain.
bool ReadPointer(std::uintptr_t address, std::uintptr_t& out, ChainFault& chain) {
    std::uintptr_t value = 0;
    if (!cameraunlock::memory::SafeRead(address, value)) {
        NoteFault(chain);
        return false;
    }
    if (value == 0) return false;
    out = value;
    return true;
}

// Reads one engine flag byte. The three gate flags are all this shape: a byte
// that is only meaningful when it could be read at all, and a chain that says
// so once when it could not.
bool ReadFlagByte(std::uintptr_t address, std::uint8_t& out, ChainFault& chain) {
    if (!cameraunlock::memory::SafeRead(address, out)) {
        NoteFault(chain);
        return false;
    }
    return true;
}

// What a read of one engine singleton's state enum found. A null singleton is a
// real answer and not a fault, which is why NoObject is not folded into Faulted:
// both network objects are created during startup and only torn down on
// shutdown, so null means the engine has not got as far as that subsystem.
enum class StateRead { Faulted, NoObject, Ok };

StateRead ReadSingletonState(std::uintptr_t static_address, unsigned state_offset,
                             std::uint32_t& out) {
    std::uintptr_t object = 0;
    if (!cameraunlock::memory::SafeRead(static_address, object)) return StateRead::Faulted;
    if (object == 0) return StateRead::NoObject;
    if (!cameraunlock::memory::SafeRead(object + state_offset, out)) return StateRead::Faulted;
    return StateRead::Ok;
}

// Reads the vtable pointer out of an object and matches it against the classes
// the profile pins. Null when the object is unreadable or is not one of them.
const ControllerClass* ClassifyController(std::uintptr_t object) {
    std::uintptr_t vtable = 0;
    if (!cameraunlock::memory::SafeRead(object, vtable)) return nullptr;
    for (const ControllerClass& candidate : g_controllers) {
        if (candidate.vtable == vtable) return &candidate;
    }
    return nullptr;
}

// One line, once, and only when the pinned slot did not hold a camera
// controller at all. The active-controller offset is the least corroborated
// number in the profile and a wrong one leaves the gate shut forever with
// nothing in the log to say why, so this walks the view manager's own bytes and
// names the slot that does hold a controller. That is the whole fix, in the log
// line that reports the fault.
void LogUnknownController(std::uintptr_t view_manager, std::uintptr_t controller) {
    if (g_logged_unknown_controller) return;
    g_logged_unknown_controller = true;

    std::uintptr_t vtable = 0;
    cameraunlock::memory::SafeRead(controller, vtable);
    Log::Line("[state] view manager +0x%X holds an object whose vtable is 0x%p - not a camera "
              "controller this profile knows. The gate reads unknown and head tracking stays off.",
              g_view.active_controller, reinterpret_cast<void*>(vtable));

    for (unsigned offset = 0; offset < kViewManagerScanBytes; offset += sizeof(std::uintptr_t)) {
        std::uintptr_t candidate = 0;
        if (!cameraunlock::memory::SafeRead(view_manager + offset, candidate) || candidate == 0) {
            continue;
        }
        const ControllerClass* found = ClassifyController(candidate);
        if (found != nullptr) {
            Log::Line("[state]   view manager +0x%X holds a %s - "
                      "view_manager_active_controller may belong there.", offset, found->name);
        }
    }
}

void LogControllerChange(const ControllerClass& active) {
    if (g_last_controller_name == active.name) return;
    g_last_controller_name = active.name;
    Log::Line("[state] active camera controller is %s", active.name);
}

}  // namespace

const char* ViewStateName(ViewState state) {
    switch (state) {
        case ViewState::Unknown:  return "unknown";
        case ViewState::NoView:   return "no view";
        case ViewState::Menu:     return "menu";
        case ViewState::Gameplay: return "gameplay";
    }
    return "unknown";
}

bool InitGameState() {
    const builds::BuildProfile& profile = builds::ActiveProfile();
    const builds::OffsetTable& offsets = profile.Offsets;
    const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    g_view = { module_base + offsets.view_manager_ptr_rva,
               offsets.view_manager_active_controller };

    // Zero means this profile never derived the chain, not that it lives at the
    // image base. Left at zero, IsGamePaused reads nothing and answers false.
    g_pause = { offsets.pause_state_ptr_rva == 0 ? 0 : module_base + offsets.pause_state_ptr_rva,
                offsets.pause_state_object,
                offsets.pause_state_paused };

    g_gameplay = { module_base + offsets.gameplay_state_ptr_rva,
                   offsets.race_phase_offset,
                   offsets.race_phase_countdown,
                   offsets.race_phase_racing,
                   offsets.countdown_ms_offset };

    g_net = { module_base + offsets.net_client_ptr_rva,
              offsets.net_client_state,
              offsets.net_client_session_lo,
              offsets.net_client_session_hi,
              module_base + offsets.net_server_ptr_rva,
              offsets.net_server_state };

    g_controllers = {{
        { module_base + offsets.garage_camera_vtable_rva,     false, "GarageCamera" },
        { module_base + offsets.car_paint_camera_vtable_rva,  false, "CarPaintCamera" },
        { module_base + offsets.car_camera_vtable_rva,        true,  "CarCamera" },
        { module_base + offsets.free_camera_vtable_rva,       true,  "FreeCamera" },
        { module_base + offsets.track_side_camera_vtable_rva, true,  "TrackSideCamera" },
        { module_base + offsets.animated_camera_vtable_rva,   true,  "AnimatedCamera" },
        { module_base + offsets.photo_mode_camera_vtable_rva, true,  "PhotoModeCamera" },
    }};

    // The static is data in the game's own image and is readable from the moment
    // it maps, whatever it happens to hold. A fault here means the RVA does not
    // land in the image at all, which is a profile that would take the gate down
    // every frame rather than once at boot.
    std::uintptr_t probe = 0;
    if (!cameraunlock::memory::SafeRead(g_view.manager_static, probe)) {
        Log::Line("[state] profile %s: the view manager static is not readable in this "
                  "image - the gameplay gate stays closed.", profile.Name);
        return false;
    }

    Log::Line("[state] view manager static 0x%p", reinterpret_cast<void*>(g_view.manager_static));
    if (g_pause.state_static == 0) {
        Log::Line("[state] profile %s has no pause chain - head tracking will keep following "
                  "through the pause menu on this build.", profile.Name);
    }
    return true;
}

ViewState ReadViewState() {
    std::uintptr_t view_manager = 0;
    if (!ReadPointer(g_view.manager_static, view_manager, g_view_chain)) {
        return ViewState::Unknown;
    }

    std::uintptr_t active_controller = 0;
    if (!cameraunlock::memory::SafeRead(view_manager + g_view.active_controller,
                                        active_controller)) {
        NoteFault(g_view_chain);
        return ViewState::Unknown;
    }
    if (active_controller == 0) return ViewState::NoView;

    // Which class of controller owns the view is the whole of the gate. The
    // menus, the garage and the livery editor run a GarageCamera or a
    // CarPaintCamera; a race runs one of the ingame family. Comparing the
    // object's vtable pointer against the classes the profile pins settles it in
    // a handful of integer compares, with no RTTI walk and no string compare on
    // the render path.
    const ControllerClass* active = ClassifyController(active_controller);
    if (active == nullptr) {
        LogUnknownController(view_manager, active_controller);
        return ViewState::Unknown;
    }
    LogControllerChange(*active);
    return active->is_gameplay ? ViewState::Gameplay : ViewState::Menu;
}

GameplayReading ReadGameplayState() {
    GameplayReading reading;

    std::uintptr_t state = 0;
    if (!ReadPointer(g_gameplay.state_static, state, g_gameplay_chain)) return reading;

    std::uint32_t phase = 0;
    if (!cameraunlock::memory::SafeRead(state + g_gameplay.phase_offset, phase)) {
        NoteFault(g_gameplay_chain);
        return reading;
    }
    reading.phase = phase;

    // Read whatever the phase, because the log line for a phase that never
    // reached the countdown is exactly where this number earns its place.
    cameraunlock::memory::SafeRead(state + g_gameplay.countdown_ms_offset,
                                  reading.countdown_ms);

    reading.running = GameplayRunningFromPhase(reading.phase, g_gameplay.countdown_phase,
                                               g_gameplay.racing_phase);
    return reading;
}

void LogGameplayState(const GameplayReading& state) {
    static std::uint32_t last_phase = 0;
    static bool known = false;

    if (known && state.phase == last_phase) return;
    last_phase = state.phase;
    known = true;

    Log::Line("[state] race phase %d (countdown %d, racing %d), countdown %d ms - gameplay %s",
              static_cast<int>(state.phase), g_gameplay.countdown_phase,
              g_gameplay.racing_phase, state.countdown_ms,
              state.running ? "running" : "not running");
}

bool IsGamePaused() {
    if (g_pause.state_static == 0) return false;

    std::uintptr_t owner = 0;
    if (!ReadPointer(g_pause.state_static, owner, g_pause_chain)) return false;

    std::uintptr_t state = 0;
    if (!ReadPointer(owner + g_pause.object, state, g_pause_chain)) return false;

    std::uint8_t paused = 0;
    if (!ReadFlagByte(state + g_pause.paused, paused, g_pause_chain)) return false;
    return paused != 0;
}

NetworkState ReadNetworkState() {
    // A missing singleton keeps the sentinel rather than being skipped: the
    // whole point of the log line built from this is to distinguish "no
    // networking yet" from "state zero", and leaving the previous value in place
    // would hide the transition.
    NetworkState state;

    std::uint32_t client = 0;
    switch (ReadSingletonState(g_net.client_static, g_net.client_state, client)) {
        case StateRead::Faulted:
            NoteFault(g_net_chain);
            break;
        case StateRead::Ok:
            state.client_state = client;
            state.multiplayer = IsClientSessionState(client, g_net.client_session_lo,
                                                     g_net.client_session_hi);
            break;
        case StateRead::NoObject:
            break;
    }

    std::uint32_t server = 0;
    switch (ReadSingletonState(g_net.server_static, g_net.server_state, server)) {
        case StateRead::Faulted:
            NoteFault(g_net_chain);
            break;
        case StateRead::Ok:
            state.server_state = server;
            if (server != 0) state.multiplayer = true;
            break;
        case StateRead::NoObject:
            break;
    }
    return state;
}

void LogNetworkState(const NetworkState& state) {
    static std::uint32_t last_client = 0;
    static std::uint32_t last_server = 0;
    static bool known = false;

    if (known && state.client_state == last_client && state.server_state == last_server) return;
    last_client = state.client_state;
    last_server = state.server_state;
    known = true;

    Log::Line("[state] network: %s - client state %d, server state %d (client counts as a "
              "session in %u..%u)",
              state.multiplayer ? "in an online session" : "not in an online session",
              static_cast<int>(state.client_state), static_cast<int>(state.server_state),
              g_net.client_session_lo, g_net.client_session_hi);
}

bool GameplayRunningFromPhase(std::uint32_t phase, unsigned countdown_phase,
                              unsigned racing_phase) {
    return phase == countdown_phase || phase == racing_phase;
}

bool IsClientSessionState(std::uint32_t state, unsigned lo, unsigned hi) {
    return state >= lo && state <= hi;
}

bool ShouldFollowHead(bool tracking_enabled, ViewState view, bool gameplay_running,
                      bool paused) {
    if (!tracking_enabled) return false;
    if (!gameplay_running) return false;
    if (paused) return false;
    return view == ViewState::Gameplay;
}

}  // namespace wf2_ht
