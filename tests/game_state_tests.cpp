// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour-locking tests for the gameplay gate's classification. Reading the
// engine's view manager needs the game; deciding what each state means for head
// tracking does not, and that decision is the part that can silently start
// modifying a camera it should have left alone.

#include "game_state.h"

#include "test_support.h"

#include <cstdio>
#include <cstring>

using namespace wf2_ht;
using wf_test::Check;

namespace {

const ViewState kAllViews[] = {
    ViewState::Unknown, ViewState::NoView, ViewState::Menu, ViewState::Gameplay,
};

void GameplayIsTheOnlyFollowingState() {
    std::printf("Only gameplay follows the head\n");

    Check(ShouldFollowHead(true, ViewState::Gameplay, true, false),
          "an unpaused race with tracking on follows");

    for (ViewState view : kAllViews) {
        if (view == ViewState::Gameplay) continue;
        Check(!ShouldFollowHead(true, view, true, false),
              "a view that is not gameplay leaves the camera alone");
    }
}

// The case the view alone gets wrong, and the reason the gameplay flags exist.
// Wreckfest 2's main menu renders through a romu::TrackSideCamera and its car
// select, paddock and results screens through a romu::CarCamera, so the
// controller class reads as gameplay in the menus exactly as it does on track.
void AMenuRenderedThroughARaceCameraIsStillAMenu() {
    std::printf("Running gameplay is required on top of the view\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(true, view, false, false),
              "no running gameplay never follows the head");
    }
}

// The case the view alone cannot see. The pause menu draws the same cockpit
// through the same CarCamera, so the controller class still reads as gameplay
// and the engine's own pause byte is the only thing left that separates a
// paused race from a running one.
void APausedRaceIsOff() {
    std::printf("A paused race does not follow the head\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(true, view, true, true),
              "a paused race never follows the head");
    }
}

// An online race is a race. The gate takes no network input at all, which is
// what this locks: nothing the mod does reaches a server or another player, so
// there is nothing for the network state to decide, and this mod follows the
// head online exactly as the Wreckfest 1 mod it is ported from does. The
// engine's network state is still read and logged, for triage rather than for
// the gate.
//
// What a test can check here is only what the signature admits: the gate
// answers from four inputs, none of them the network, so an online race and an
// offline one are the same call. Putting the term back would mean adding a
// parameter, and this call is what would stop compiling.
void AnOnlineRaceFollowsTheHeadLikeAnyOther() {
    std::printf("An online race follows the head\n");

    Check(ShouldFollowHead(true, ViewState::Gameplay, true, false),
          "the gate has no network input that could shut it");
}

void TheToggleIsRespected() {
    std::printf("The player's toggle shuts the gate on its own\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(false, view, true, false),
              "tracking toggled off never follows the head");
    }
}

void EveryViewStateIsNamed() {
    // The name is what a "head tracking does nothing" bug report is triaged
    // from, so an unnamed state is a state nobody can diagnose.
    std::printf("Every view state has a name for the log\n");

    for (ViewState view : kAllViews) {
        const char* name = ViewStateName(view);
        Check(name != nullptr && name[0] != '\0', "the state has a non-empty name");
    }

    Check(std::strcmp(ViewStateName(ViewState::Gameplay),
                      ViewStateName(ViewState::Menu)) != 0,
          "gameplay and menu do not share a name");
}

// The engine's race phase rule, with the two phase values the shipped profile
// pins. The countdown phase is half the point: the gate used to open on the
// engine's green-light byte, so a player sitting on the grid looking around got
// nothing until the lights went out.
//
// The other half is that the phase is the WHOLE rule. It used to be ANDed with
// the engine's frontend flag, which is correct in single player and wrong in
// multiplayer - measured on the shipped build, the flag stays raised for the
// whole of an online race the player is driving in, so the gate stayed shut
// through every multiplayer race while opening for the same player spectating
// one. Anything added back here has to be checked in an online race the tester
// actually drives in, not one they watch.
void TheRacePhaseRule() {
    std::printf("The race phase gameplay rule\n");

    const unsigned countdown = 2;
    const unsigned racing = 3;

    // On the grid, counting down. This is the case the rule exists for.
    Check(GameplayRunningFromPhase(countdown, countdown, racing),
          "the grid countdown is running gameplay");

    // Green light onwards.
    Check(GameplayRunningFromPhase(racing, countdown, racing),
          "a running race is running gameplay");

    // Every other phase the engine writes is off. 0 is the torn-down session
    // every menu sits in, 1 is the brief pre-countdown step, and 4 and 5 were
    // never established from the code, so they are off by the same rule that
    // keeps an unknown camera controller out of the gate rather than by a claim
    // about what they mean.
    const std::uint32_t kOtherPhases[] = { 0, 1, 4, 5 };
    for (std::uint32_t phase : kOtherPhases) {
        Check(!GameplayRunningFromPhase(phase, countdown, racing),
              "a phase that is neither pinned value is not running gameplay");
    }

    // A phase that could not be read at all fails CLOSED.
    Check(!GameplayRunningFromPhase(GameplayReading::kPhaseUnavailable, countdown, racing),
          "an unreadable phase: not running");
}

// The engine's own leaf predicate is a range compare, and the connecting state
// sits below the range deliberately: NOTES measured 0 -> 2 (connecting) -> 4 ->
// 5 -> 6 -> 0. This decides nothing now that the gate ignores the network, but
// it is what the log calls an online session, and a log that says "not in an
// online session" through a whole online race is worse than one that says
// nothing.
void TheClientSessionRangeMatchesTheEngine() {
    std::printf("The client session range\n");

    Check(!IsClientSessionState(0, 3, 6), "state 0, no session");
    Check(!IsClientSessionState(2, 3, 6), "state 2, connecting is not yet a session");
    Check(IsClientSessionState(3, 3, 6), "state 3, in a session");
    Check(IsClientSessionState(4, 3, 6), "state 4, in a session");
    Check(IsClientSessionState(6, 3, 6), "state 6, in a session");
    Check(!IsClientSessionState(7, 3, 6), "state 7, past the range");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - gameplay gate tests\n");
    std::printf("=======================================================\n");
    GameplayIsTheOnlyFollowingState();
    AMenuRenderedThroughARaceCameraIsStillAMenu();
    APausedRaceIsOff();
    AnOnlineRaceFollowsTheHeadLikeAnyOther();
    TheToggleIsRespected();
    TheRacePhaseRule();
    TheClientSessionRangeMatchesTheEngine();
    EveryViewStateIsNamed();
    return wf_test::Summary("gameplay gate");
}
