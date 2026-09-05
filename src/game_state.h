// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace wf2_ht {

// What the engine is showing right now, as far as head tracking cares.
enum class ViewState {
    // The view manager or its active camera controller could not be read. The
    // mod treats this as "not gameplay" rather than tracking blind: this state
    // covers the whole of startup, when the render camera exists but nothing
    // has decided what it is looking at yet.
    Unknown,
    // No camera controller is active. Loading screens and the boot sequence.
    // Three of the engine's camera-type values reach this deliberately - the
    // view manager's own dispatch table stores a null controller for them.
    NoView,
    // A romu::GarageCamera or romu::CarPaintCamera owns the view: the menus, the
    // garage and the livery editor. A different camera on a different scene from
    // the one the player drives in, so a head pose composed for the cockpit has
    // no meaning here.
    Menu,
    // One of the romu ingame cameras owns the view: the car camera, a trackside
    // camera, a scripted camera, the free camera or photo mode. Includes
    // replays, which render a real 3D view the player is watching.
    Gameplay,
};

const char* ViewStateName(ViewState state);

// Resolves the view manager static and the camera controller vtables from the
// active build profile, so the per-frame reads below are pointer loads and
// integer compares rather than a profile lookup. Runs after
// builds::SelectProfile() has matched, and returns false if the static falls
// outside the running module - in which case the gate reports Unknown forever
// and the mod never follows the head.
bool InitGameState();

// The current view, read fresh. Call from the render thread.
ViewState ReadViewState();

// One reading of the engine's race phase, and what the gate makes of it.
//
// Both in one struct because the log line and the gate must describe the same
// reading - taken separately they are two passes over engine state that can move
// between them, so the line accompanying a decision could name the values behind
// a different one.
struct GameplayReading {
    // What an unreadable phase reports, and what one reports before the engine
    // has built the singleton. Outside the range the engine's enum holds, so it
    // cannot be confused with phase 0.
    static constexpr std::uint32_t kPhaseUnavailable = 0xFFFFFFFFu;

    // The engine's race state machine. The two values the mod follows through
    // are pinned by the profile; the rest are logged by number rather than
    // named, because only those two were established from the code.
    std::uint32_t phase = kPhaseUnavailable;

    // Milliseconds until the green light: negative for the length of the grid
    // countdown, zero the frame it fires. Only meaningful in the countdown
    // phase, and read for the log rather than for the gate - a dword counting up
    // at wall-clock rate and stopping at zero is what says the phase read is
    // landing on the right field.
    std::int32_t countdown_ms = 0;

    // The gate's answer: gameplay is running and head tracking may follow.
    bool running = false;
};

// Reads the race phase and the countdown counter out of the romu::GameplayState
// singleton in one pass.
//
// Fails CLOSED: an unreadable field, or one a patch has moved, reports no
// gameplay and takes head tracking off. Call from the render thread.
GameplayReading ReadGameplayState();

// One line whenever the phase changes, naming the raw value, the countdown
// counter beside it and what the gate made of the pair. Edge-triggered on the
// phase alone, so a race writes a handful of lines rather than one per frame.
//
// This is what a "head tracking did not come on" report needs: the answer alone
// cannot say whether the phase never reached the countdown or whether a patch
// moved a field and the read is landing on unrelated bytes. The countdown
// counter tells those apart by itself - it counts up at wall-clock rate and
// stops at zero, and nothing else in the object does. Call from the render
// thread.
void LogGameplayState(const GameplayReading& state);

// True while the engine's global pause is raised: the pause menu is up, or the
// game window has lost focus. False when it is down, and false whenever the
// chain cannot be read.
//
// Fail OPEN, which is the opposite call to everything else in this file, and
// the reason is what a wrong answer costs. The pause chain is pinned per build,
// so a patch moving it makes the read fail; treating that as "paused" would
// take head tracking down everywhere on the first stale offset, and being wrong
// the other way only means the head still moves the camera behind the pause
// menu until the profile is updated. A cosmetic regression beats a dead mod.
//
// An address that cannot be READ is logged once, so a chain pointing into
// nothing stays visible. A link that reads as null is not: that is what each of
// these singletons holds until the engine builds it, so reporting it would put
// a line in every log. A patch that moves a field onto a permanently-zero slot
// therefore goes unreported - see ReadPointer in the .cpp. Call from the render
// thread.
bool IsGamePaused();

// One reading of both network singletons: whether this process is in an online
// session, and the raw state enums that answer came from. Both in one struct
// because the log line must describe a single reading - taken separately they
// are two passes over engine state that can move between them, so the summary
// could name values from a different one than the raw numbers beside it.
struct NetworkState {
    // What a singleton the engine has not constructed yet reports, and what an
    // unreadable one reports too. Outside the range either enum can hold, so it
    // cannot be confused with state zero.
    static constexpr std::uint32_t kUnavailable = 0xFFFFFFFFu;

    bool multiplayer = false;
    std::uint32_t client_state = kUnavailable;
    std::uint32_t server_state = kUnavailable;
};

// Reads both singletons. `multiplayer` is true while the engine is in a network
// session - a client connected to a server, or a server this process is running
// for other players to join.
//
// Read from the engine's own state fields rather than by calling its
// predicates: the two leaf functions that answer this in the image are a null
// check plus a range compare on one enum each, and reproducing that costs two
// loads and a subtraction while calling it would mean an indirect call through
// an address a game patch can move under us.
//
// This is diagnostic only. Head tracking follows the head online exactly as it
// does offline, so nothing here gates anything and a chain that faults costs a
// log line rather than the mod - it reports no session and says so once through
// the fault latch, rather than guessing in either direction. Call from the
// render thread.
NetworkState ReadNetworkState();

// One line whenever either state enum changes, naming the raw values and what
// they mean. Edge-triggered, so a session that never goes online writes one line
// and then nothing.
//
// Worth its place even though the gate ignores it: an online race is the one
// place a report cannot be reproduced by loading the same track alone, so a
// "the camera did something strange" report needs the log to say whether the
// player was in a session at the time. Call from the render thread.
void LogNetworkState(const NetworkState& state);

// The rule ReadGameplayState applies, split out from the engine reads so it can
// be checked without a game running. `phase` is the engine's race state machine
// and `countdown_phase` / `racing_phase` the two values the profile pins.
//
// Head tracking follows through both pinned phases, which is what puts it on the
// grid: the countdown phase runs from the moment the participants are placed
// until the counter beside it reaches zero, and the racing phase from the green
// light until the race is torn down. Every other phase value is off, the
// unavailable sentinel included, so an unreadable phase fails CLOSED.
//
// The phase is the whole rule. It used to be ANDed with the engine's frontend
// flag, and that flag stays raised for the whole of a multiplayer race the
// player is driving in, which shut the gate through every online race while
// leaving it open for the same player spectating one.
bool GameplayRunningFromPhase(std::uint32_t phase, unsigned countdown_phase,
                              unsigned racing_phase);

// Whether the engine's client-session enum is in the range that means "in a
// network session", reproducing the range compare in the engine's own leaf
// predicate. The connecting state sits below the range on purpose: a connection
// attempt is not yet a session.
bool IsClientSessionState(std::uint32_t state, unsigned lo, unsigned hi);

// The gate: true when the camera hook should compose the head pose into the
// frame it is about to draw. Pure, so the classification can be tested without
// a game running.
//
// Anything that is not Gameplay is off, Unknown included - a view the mod could
// not read is not one to modify - and running gameplay is required on top of
// it, because the menus render through the same camera classes a race does. The
// engine's pause is off on top of both: the pause menu renders the same cockpit
// from the same race camera with gameplay still running, so nothing else in the
// gate can see it.
//
// Pausing does cost two jumps - the view snaps to the engine's camera when the
// menu comes up and back to the tracked one when it closes - and that is the
// point rather than a side effect. The pipeline keeps advancing while the gate
// is shut, so the frame tracking resumes on is composed from where the head is
// then, not from where it was when the player paused.
//
// An online race is not a case. Nothing this mod does is visible to a server or
// to another player - the pose is composed into the camera transform for the
// frame being drawn and taken back out before the engine interpolates from it,
// so car control, physics and everything sent over the wire read the camera the
// game computed - so a race is a race whoever else is in it, and this matches
// what the Wreckfest 1 mod does. The network state is still read and logged, for
// triage rather than for the gate.
bool ShouldFollowHead(bool tracking_enabled, ViewState view, bool gameplay_running, bool paused);

}  // namespace wf2_ht
