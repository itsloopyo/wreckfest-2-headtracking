// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>

#include "builds/build_profile.h"

namespace wf2_ht::builds {

// Append-only. Newest build first: the top entry is the diagnostic primary
// that the "unknown build" log line compares against to say whether the
// running EXE is newer or older than anything this mod knows about.
extern const BuildProfile kSteamProfile_20260709;

extern const BuildProfile* const kKnownProfiles[];
extern const std::size_t kKnownProfileCount;

enum class ProfileSelection {
    Matched,       // Fingerprint matched a complete profile; safe to hook.
    Incomplete,    // Fingerprint matched, but the profile is a placeholder.
    NoMatch,       // Unknown build.
};

// Fingerprints the running module and selects a profile. Must run before any
// hook is installed; anything other than Matched leaves the mod dormant.
ProfileSelection SelectProfile(void* moduleBase);

// Valid only after SelectProfile() returned Matched.
const BuildProfile& ActiveProfile();

}  // namespace wf2_ht::builds
