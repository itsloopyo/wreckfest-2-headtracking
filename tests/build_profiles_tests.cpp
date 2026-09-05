// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The build registry is what keeps the mod working for a user who has not taken
// the latest game patch, and its rules are policy rather than anything the
// compiler can check: profiles are append-only, an existing profile's RVAs are
// never edited in place, and the newest one sits at the top because that is the
// entry the "unknown build" diagnostic compares against.
//
// Where the expected values come from matters, because a number copied out of
// steam_offsets.cpp would agree with any edit to steam_offsets.cpp - including
// the edit this exists to stop, a session answering a patch by rewriting the
// 20260709 profile instead of appending a new one, stranding every user still
// on that build.
//
// The fingerprint, the struct offsets and the gate statics below are taken from
// .lab/NOTES.md, which records how each was derived from the shipped EXE. The
// two function RVAs are recorded there too, under "How the addresses were
// found", and were added to it alongside this file - they had only ever existed
// in the profile, which is the one place a restatement is worthless.

#include "builds/build_registry.h"

#include "test_support.h"

#include <cstdio>
#include <string>

using namespace wf2_ht::builds;
using wf_test::Check;

namespace {

void CheckOffset(unsigned int actual, unsigned int expected, const char* what) {
    if (actual == expected) {
        std::printf("  ok:   %s\n", what);
        return;
    }
    std::printf("  FAIL: %s (expected 0x%X, got 0x%X)\n", what, expected, actual);
    ++wf_test::g_failures;
}

// Steam app 1203190, Wreckfest2.exe, in-game build string 369134-PC-FINAL.
void TheShippedSteamProfileTest() {
    std::printf("The steam-win64-20260709 profile\n");

    const BuildProfile& p = kSteamProfile_20260709;
    Check(std::string(p.Name) == "steam-win64-20260709", "is named store-platform-YYYYMMDD");

    CheckOffset(p.Fingerprint.TimeDateStamp, 0x6A4F992Cu, "TimeDateStamp");
    CheckOffset(p.Fingerprint.SizeOfImage, 0x04C9B000u, "SizeOfImage");
    // Bugbear's linker never stamps one, the same as Wreckfest 1. Pinned so a
    // later session does not "helpfully" invent a value the running EXE will
    // never match.
    CheckOffset(p.Fingerprint.CheckSum, 0x00000000u, "CheckSum (never stamped by this linker)");

    CheckOffset(p.Offsets.view_manager_update_rva, 0x000C7BD0u, "ViewManager::Update RVA");
    CheckOffset(p.Offsets.camera_view_matrix_rva, 0x00A22C90u, "Camera::UpdateViewMatrix RVA");
    CheckOffset(p.Offsets.camera_world_transform, 0x10u, "camera-to-world transform offset");
    CheckOffset(p.Offsets.camera_world_transform_floats, 16u, "a 4x4 of floats");
    CheckOffset(p.Offsets.view_manager_ptr_rva, 0x013FEE48u, "view manager static");
    CheckOffset(p.Offsets.view_manager_render_camera, 0x1210u, "embedded render camera offset");
    CheckOffset(p.Offsets.view_manager_active_controller, 0x28u, "active controller offset");
    CheckOffset(p.Offsets.gameplay_state_ptr_rva, 0x013FEE58u, "GameplayState static");
    CheckOffset(p.Offsets.race_phase_offset, 0x13F8u, "race phase offset");
    CheckOffset(p.Offsets.race_phase_countdown, 2u, "the grid countdown is phase 2");
    CheckOffset(p.Offsets.race_phase_racing, 3u, "a running race is phase 3");
    CheckOffset(p.Offsets.countdown_ms_offset, 0x139Cu, "countdown milliseconds offset");
    CheckOffset(p.Offsets.net_client_session_lo, 3u, "client session range starts at 3");
    CheckOffset(p.Offsets.net_client_session_hi, 6u, "client session range ends at 6");

    Check(IsProfileComplete(p), "is complete, so the mod will engage on it");
}

// The completeness gate is what stops a placeholder profile - fingerprint
// derived, offsets not - from being activated. It is a sample rather than a
// full audit (the controller vtables and the network offsets are not in it),
// and what it must cover is the camera path, because that is where a zero is
// not merely wrong but destructive: a zero camera_world_transform makes the
// hook memcpy 64 bytes over the camera's vtable pointer, and a zero
// view_manager_render_camera makes the restore never match, so the pose
// compounds into engine state frame on frame.
void TheCompletenessGateTest() {
    std::printf("The profile completeness gate\n");

    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        Check(IsProfileComplete(*kKnownProfiles[i]),
              "every profile in the registry is complete");
    }

    BuildProfile placeholder = kSteamProfile_20260709;
    placeholder.Offsets.camera_world_transform = 0;
    Check(!IsProfileComplete(placeholder), "a zero camera transform offset is refused");

    placeholder = kSteamProfile_20260709;
    placeholder.Offsets.view_manager_render_camera = 0;
    Check(!IsProfileComplete(placeholder), "a zero render camera offset is refused");

    placeholder = kSteamProfile_20260709;
    placeholder.Offsets.camera_view_matrix_rva = 0;
    Check(!IsProfileComplete(placeholder), "a zero view matrix RVA is refused");

    placeholder = kSteamProfile_20260709;
    placeholder.Offsets.gameplay_state_ptr_rva = 0;
    Check(!IsProfileComplete(placeholder), "a zero gate static is refused");

    placeholder = kSteamProfile_20260709;
    placeholder.Offsets.race_phase_offset = 0;
    Check(!IsProfileComplete(placeholder), "a zero race phase offset is refused");
}

void TheRegistryTest() {
    std::printf("The registry\n");

    if (!Check(kKnownProfileCount >= 1, "holds at least one profile")) return;

    // Append-only means the count only ever grows. Stated rather than asserted
    // against a number, which would have to be edited by the very commit this
    // is protecting against.
    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        Check(kKnownProfiles[i] != nullptr, "every entry points at a profile");
    }

    // Newest first: LogUnknownBuild compares the running EXE against
    // kKnownProfiles[0] to say whether the user is ahead of the mod or behind
    // it, so an out-of-order registry tells them to do the opposite of the
    // right thing.
    for (std::size_t i = 1; i < kKnownProfileCount; ++i) {
        Check(kKnownProfiles[i - 1]->Fingerprint.TimeDateStamp
                  > kKnownProfiles[i]->Fingerprint.TimeDateStamp,
              "profiles are ordered newest first");
    }

    // Two profiles with the same fingerprint means the second is unreachable:
    // SelectProfile returns on the first match.
    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        for (std::size_t j = i + 1; j < kKnownProfileCount; ++j) {
            const bool same =
                kKnownProfiles[i]->Fingerprint.TimeDateStamp
                    == kKnownProfiles[j]->Fingerprint.TimeDateStamp
                && kKnownProfiles[i]->Fingerprint.SizeOfImage
                    == kKnownProfiles[j]->Fingerprint.SizeOfImage;
            Check(!same, "no two profiles share a fingerprint");
            Check(std::string(kKnownProfiles[i]->Name) != kKnownProfiles[j]->Name,
                  "no two profiles share a name");
        }
    }
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - build profile tests\n");
    std::printf("========================================================\n");
    TheShippedSteamProfileTest();
    TheCompletenessGateTest();
    TheRegistryTest();
    return wf_test::Summary("build profiles");
}
