// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_registry.h"

#include "logging.h"

using cameraunlock::memory::FingerprintMismatch;
using cameraunlock::memory::PeFingerprint;

namespace wf2_ht::builds {

const BuildProfile* const kKnownProfiles[] = {
    &kSteamProfile_20260709,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

const BuildProfile* g_active = nullptr;

// Says which side of the known builds the running EXE fell on, against the
// diagnostic primary - the newest profile in the registry, which is why
// kKnownProfiles is ordered newest first. This is the whole of what a user sees
// when their build is unrecognised, so it has to name the direction rather than
// just refusing.
void LogUnknownBuild(const PeFingerprint& running) {
    const BuildProfile& primary = *kKnownProfiles[0];
    switch (cameraunlock::memory::ClassifyMismatch(running, primary.Fingerprint)) {
    case FingerprintMismatch::Newer:
        Log::Line("[build] this game build is NEWER than any build this mod knows about "
                  "(newest known: %s). Check the mod's releases page for an update.", primary.Name);
        break;
    case FingerprintMismatch::Older:
        Log::Line("[build] this game build is OLDER than any build this mod knows about "
                  "(newest known: %s). Let Steam finish updating the game.", primary.Name);
        break;
    case FingerprintMismatch::Differs:
        Log::Line("[build] this EXE carries a known build timestamp but a different image "
                  "size or checksum, so it is not a build this mod has offsets for.");
        break;
    }
    Log::Line("[build] staying fully dormant: no hooks installed, the game runs vanilla.");
}

}  // namespace

const BuildProfile& ActiveProfile() { return *g_active; }

ProfileSelection SelectProfile(void* moduleBase) {
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(moduleBase, running)) {
        Log::Line("[build] could not read PE headers of the running module; staying dormant");
        return ProfileSelection::NoMatch;
    }

    Log::Line("[build] running EXE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& p = *kKnownProfiles[i];
        const bool match = running.Matches(p.Fingerprint);
        Log::Line("[build]   vs %-24s TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X -> %s",
                  p.Name, p.Fingerprint.TimeDateStamp, p.Fingerprint.SizeOfImage,
                  p.Fingerprint.CheckSum, match ? "MATCH" : "no");
        if (!match) continue;

        if (!IsProfileComplete(p)) {
            Log::Line("[build] profile %s is a placeholder (offsets not derived yet); staying dormant", p.Name);
            return ProfileSelection::Incomplete;
        }
        g_active = &p;
        Log::Line("[build] activated profile %s", p.Name);
        return ProfileSelection::Matched;
    }

    LogUnknownBuild(running);
    return ProfileSelection::NoMatch;
}

}  // namespace wf2_ht::builds
