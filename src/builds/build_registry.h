// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>

#include "builds/build_profile.h"

namespace wf2_ht::builds {

// Append-only. Newest build first: the top entry is the diagnostic primary the
// "unknown build" log line names.
extern const BuildProfile kSteamProfile_20261022;
extern const BuildProfile kSteamProfile_20261015;
extern const BuildProfile kSteamProfile_20260709;

extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

enum class ProfileSelection {
    Matched,       // Fingerprint matched a complete measured profile; safe to hook.
    Discovered,    // No measured profile, but runtime discovery resolved every value.
    Incomplete,    // Fingerprint matched, but the profile is a placeholder.
    Inconsistent,  // Fingerprint matched, but runtime discovery disagrees with the profile.
    NoMatch,       // Unknown build, and runtime discovery failed.
};

// Fingerprints the running module, runs runtime discovery over it, and selects
// what the mod runs on: the measured profile when the fingerprint has one, with
// the discovered values cross-checked against it in the log, otherwise the
// discovered values. Must run before any hook is installed. Incomplete,
// Inconsistent and NoMatch leave the mod dormant with no active profile.
ProfileSelection SelectProfile(void* moduleBase);

// Valid only after SelectProfile() returned Matched or Discovered.
const BuildProfile& ActiveProfile();
bool HasActiveProfile();

}  // namespace wf2_ht::builds
