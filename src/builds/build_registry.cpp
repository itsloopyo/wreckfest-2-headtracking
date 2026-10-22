// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_registry.h"

#include <windows.h>

#include <chrono>
#include <string>

#include "builds/runtime_discovery.h"
#include "logging.h"

using cameraunlock::memory::PeFingerprint;

namespace wf2_ht::builds {

const BuildProfile* const kKnownProfiles[] = {
    &kSteamProfile_20261022,
    &kSteamProfile_20261015,
    &kSteamProfile_20260709,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

const BuildProfile* g_active = nullptr;
BuildProfile g_discovered{ "runtime-discovery", {}, {} };

// The whole of what a user sees when the mod cannot run. It names the newest
// profile for comparison and nothing more: a PE timestamp is not established as
// a build date for this game, so it says nothing about which build is newer.
void LogUnknownBuild() {
#ifdef WF2_DEV_DISCOVERY_ONLY
    Log::Line("[build] runtime discovery failed and this dev build ignores the measured profiles.");
#else
    Log::Line("[build] no measured profile has this fingerprint (newest profile: %s) and runtime discovery "
              "failed. Check the mod's releases page for a version that supports this game build.",
              kKnownProfiles[0]->Name);
#endif
    Log::Line("[build] staying fully dormant: no hooks installed, the game runs vanilla.");
}

// Discovery reads the whole image, so every page of it has to be committed and
// readable first. A packer that maps sections no-access until they first run
// would otherwise fault the scan.
bool ImageIsReadable(const std::uint8_t* base, std::size_t size) {
    const std::uint8_t* at = base;
    while (at < base + size) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(at, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        const DWORD access = mbi.Protect & 0xFF;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || access == PAGE_NOACCESS) return false;
        at = static_cast<const std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool Discover(void* moduleBase, const PeFingerprint& running, OffsetTable& out) {
    const auto* base = static_cast<const std::uint8_t*>(moduleBase);
    if (!ImageIsReadable(base, running.SizeOfImage)) {
        Log::Line("[build] runtime discovery failed: part of the image is not readable, so it was not scanned");
        return false;
    }
    std::string failure;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = DiscoverOffsets({ base, running.SizeOfImage, reinterpret_cast<std::uintptr_t>(moduleBase) },
                                    out, failure);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (!ok) {
        Log::Line("[build] runtime discovery failed after %lld ms: %s", ms, failure.c_str());
    } else if (failure.empty()) {
        Log::Line("[build] runtime discovery resolved every value in %lld ms", ms);
    } else {
        Log::Line("[build] runtime discovery resolved every required value in %lld ms; the pause chain was "
                  "not found (%s), so pause is not detected on the discovered route", ms, failure.c_str());
    }
    return ok;
}

// A measured profile and the discovered values describe the same build, so any
// difference is a fault in one of them, and nothing says which. Each difference
// is logged and the count returned. A pause chain discovery did not find is a
// gap, not a difference.
int CrossCheck(const BuildProfile& profile, const OffsetTable& found) {
    const bool pause_found = found.pause_state_ptr_rva != 0;
    int differences = 0;
    for (std::size_t i = 0; i < kOffsetFieldCount; ++i) {
        const OffsetField& f = kOffsetFields[i];
        if (!pause_found && (f.member == &OffsetTable::pause_state_ptr_rva || f.member == &OffsetTable::pause_state_object
                             || f.member == &OffsetTable::pause_state_paused)) {
            continue;
        }
        const unsigned int want = profile.Offsets.*f.member;
        const unsigned int got = found.*f.member;
        if (want == got) continue;
        ++differences;
        Log::Line("[build] runtime discovery DISAGREES with %s on %s: profile 0x%X, discovered 0x%X",
                  profile.Name, f.name, want, got);
    }
    if (differences == 0) {
        Log::Line("[build] runtime discovery agrees with %s on every value%s", profile.Name,
                  pause_found ? "" : " it resolved");
    }
    return differences;
}

}  // namespace

const BuildProfile& ActiveProfile() { return *g_active; }
bool HasActiveProfile() { return g_active != nullptr; }

ProfileSelection SelectProfile(void* moduleBase) {
    g_active = nullptr;
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(moduleBase, running)) {
        Log::Line("[build] could not read PE headers of the running module; staying dormant");
        return ProfileSelection::NoMatch;
    }

    Log::Line("[build] running EXE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    OffsetTable found{};
    const bool discovered = Discover(moduleBase, running, found);

#ifdef WF2_DEV_DISCOVERY_ONLY
    Log::Line("[build] dev build: measured profiles ignored, running on runtime discovery alone");
    const std::size_t profile_count = 0;
#else
    const std::size_t profile_count = kKnownProfileCount;
#endif
    for (std::size_t i = 0; i < profile_count; ++i) {
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
        if (discovered && CrossCheck(p, found) != 0) {
            Log::Line("[build] profile %s and runtime discovery disagree, so neither can be trusted.", p.Name);
            Log::Line("[build] staying fully dormant: no hooks installed, the game runs vanilla.");
            return ProfileSelection::Inconsistent;
        }
        g_active = &p;
        Log::Line("[build] activated profile %s", p.Name);
        return ProfileSelection::Matched;
    }

    if (!discovered) {
        LogUnknownBuild();
        return ProfileSelection::NoMatch;
    }
    g_discovered.Fingerprint = running;
    g_discovered.Offsets = found;
    g_active = &g_discovered;
    Log::Line("[build] no measured profile has this fingerprint; running on the values runtime discovery resolved:");
    for (std::size_t i = 0; i < kOffsetFieldCount; ++i) {
        Log::Line("[build]   %-32s 0x%X", kOffsetFields[i].name, found.*kOffsetFields[i].member);
    }
    return ProfileSelection::Discovered;
}

}  // namespace wf2_ht::builds
