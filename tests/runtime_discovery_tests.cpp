// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Runtime discovery, two ways. Synthetic images built by discovery_fixture.h
// prove the resolver follows the structure wherever it is laid out and refuses
// an image with any piece of it missing or doubled. Every installed
// Wreckfest2.exe named in WF2_GAME_EXES (semicolon separated; run-tests.ps1
// fills it from core's game path detection) is then mapped the way the loader
// maps it and resolved from scratch; where its fingerprint matches a measured
// profile, every field has to agree with that profile.
//
// That agreement is not independent evidence for the 20261022 profile: it was
// derived statically with the same anchors the resolver uses, so the two can be
// wrong together. What is independent is in .lab/NOTES.md (the hook RVAs
// running in game on that build) and the earlier profiles, measured live, whose
// EXEs are no longer on disk to run this check against.

#include "builds/build_registry.h"
#include "builds/runtime_discovery.h"

#include "discovery_fixture.h"
#include "test_support.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace wf2_ht::builds;
using wf_test::Check;

namespace {

struct SectionRange {
    std::string name;
    std::uint32_t rva = 0, size = 0;
};

struct MappedImage {
    std::vector<std::uint8_t> bytes;
    std::uint64_t image_base = 0;
    cameraunlock::memory::PeFingerprint fingerprint{};
    std::vector<SectionRange> sections;

    bool In(const char* name, std::uint32_t rva) const {
        for (const SectionRange& s : sections) {
            if (s.name == name && rva >= s.rva && rva < s.rva + s.size) return true;
        }
        return false;
    }
};

std::uint32_t U32(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint32_t x;
    std::memcpy(&x, b.data() + at, 4);
    return x;
}
std::uint16_t U16(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint16_t x;
    std::memcpy(&x, b.data() + at, 2);
    return x;
}

bool MapFromFile(const std::string& path, MappedImage& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.size() < 0x400) return false;
    const std::uint32_t nt = U32(file, 0x3C);
    const std::uint32_t opt = nt + 0x18;
    const std::uint32_t size_of_image = U32(file, opt + 0x38);
    const std::uint32_t size_of_headers = U32(file, opt + 0x3C);
    std::memcpy(&out.image_base, file.data() + opt + 0x18, 8);
    out.fingerprint = { U32(file, nt + 8), size_of_image, U32(file, opt + 0x40) };

    out.bytes.assign(size_of_image, 0);
    std::memcpy(out.bytes.data(), file.data(), size_of_headers);
    const std::uint16_t nsec = U16(file, nt + 6);
    const std::uint32_t sec0 = opt + U16(file, nt + 0x14);
    for (std::uint16_t i = 0; i < nsec; ++i) {
        const std::uint32_t s = sec0 + i * 40u;
        const std::uint32_t vsize = U32(file, s + 8), rva = U32(file, s + 12);
        const std::uint32_t raw_size = U32(file, s + 16), raw = U32(file, s + 20);
        const std::uint32_t n = raw_size < vsize ? raw_size : vsize;
        if (raw + n > file.size() || rva + n > size_of_image) return false;
        std::memcpy(out.bytes.data() + rva, file.data() + raw, n);
        char name[9] = {};
        std::memcpy(name, file.data() + s, 8);
        out.sections.push_back({ name, rva, vsize });
    }
    return true;
}

void InstalledImageTest(const std::string& path) {
    std::printf("Runtime discovery on %s\n", path.c_str());
    MappedImage img;
    if (!Check(MapFromFile(path, img), "the EXE maps")) return;

    OffsetTable found{};
    std::string failure;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = DiscoverOffsets({ img.bytes.data(), img.bytes.size(), img.image_base }, found, failure);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  discovery took %lld ms\n", static_cast<long long>(ms));
    if (!ok) std::printf("  discovery failed: %s\n", failure.c_str());
    if (!Check(ok, "discovery resolves every value")) return;
    Check(failure.empty(), "including the pause chain");

    Check(img.In(".text", found.view_manager_update_rva) && img.In(".text", found.camera_view_matrix_rva),
          "both hook targets are in .text");
    Check(img.In(".rdata", found.garage_camera_vtable_rva) && img.In(".rdata", found.photo_mode_camera_vtable_rva),
          "the controller vtables are in .rdata");
    Check(img.In(".data", found.view_manager_ptr_rva) && img.In(".data", found.gameplay_state_ptr_rva)
              && img.In(".data", found.pause_state_ptr_rva) && img.In(".data", found.net_client_ptr_rva)
              && img.In(".data", found.net_server_ptr_rva),
          "every singleton static is in .data");

    const BuildProfile* known = nullptr;
    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        if (img.fingerprint.Matches(kKnownProfiles[i]->Fingerprint)) known = kKnownProfiles[i];
    }
    for (std::size_t i = 0; i < kOffsetFieldCount; ++i) {
        const OffsetField& f = kOffsetFields[i];
        const unsigned int got = found.*f.member;
        if (known == nullptr) {
            std::printf("  %-32s 0x%X\n", f.name, got);
            continue;
        }
        const unsigned int want = known->Offsets.*f.member;
        char what[160];
        std::snprintf(what, sizeof(what), "%s matches %s (0x%X, discovered 0x%X)", f.name, known->Name, want, got);
        Check(got == want, what);
    }
    if (known == nullptr) {
        std::printf("  no measured profile has this fingerprint: values printed above, checked for their sections only\n");
    }
}

using wf_fixture::Faults;
using wf_fixture::Fixture;
using wf_fixture::Layout;

bool Discover(const Fixture& fx, OffsetTable& out, std::string& failure) {
    return DiscoverOffsets({ fx.image.data(), fx.image.size(), fx.PointerBase() }, out, failure);
}

void CheckTable(const OffsetTable& got, const OffsetTable& want) {
    for (std::size_t i = 0; i < kOffsetFieldCount; ++i) {
        const OffsetField& f = kOffsetFields[i];
        char what[160];
        std::snprintf(what, sizeof(what), "%s is 0x%X (got 0x%X)", f.name, want.*f.member, got.*f.member);
        Check(got.*f.member == want.*f.member, what);
    }
}

void Resolves(const char* title, const Layout& layout, const Faults& faults = {}) {
    std::printf("%s\n", title);
    const Fixture fx(layout, faults, false);
    OffsetTable got{};
    std::string failure;
    const bool ok = Discover(fx, got, failure);
    if (!ok) std::printf("  discovery failed: %s\n", failure.c_str());
    if (!Check(ok, "discovery resolves every value")) return;
    Check(failure.empty(), "with nothing left unresolved");
    CheckTable(got, fx.expected);
}

// The pause chain is optional: without it the rest still resolves, and the
// three pause fields are left zero with the failure named.
void ResolvesWithoutPause(const char* title, const Faults& faults, const char* expect_failure) {
    std::printf("%s\n", title);
    const Fixture fx({}, faults, false);
    OffsetTable got{};
    std::string failure;
    const bool ok = Discover(fx, got, failure);
    std::printf("  failure: %s\n", failure.c_str());
    if (!Check(ok, "discovery still resolves the required values")) return;
    Check(failure == expect_failure, "and names the pause check that failed");
    OffsetTable want = fx.expected;
    want.pause_state_ptr_rva = 0;
    want.pause_state_object = 0;
    want.pause_state_paused = 0;
    CheckTable(got, want);
}

// A layout that moves every function, static and field the resolver reports.
Layout Relocated() {
    Layout l;
    l.pad = 0x40;
    l.rdata_pad = 0x180;
    l.reverse_order = true;
    l.camera_near = 0x90;
    l.render_camera = 0x1460;
    l.camera_stride = 0x320;
    l.transform = 0x20;
    l.view_matrix = 0xB0;
    l.active_controller = 0x30;
    l.controller_base = 0x480;
    l.gameplay_size = 0x1800;
    l.race_phase = 0x1700;
    l.countdown = 0x16A0;
    l.phase_countdown = 5;
    l.pause_object = 0x78;
    l.pause_flag = 0x61;
    l.client_state = 0x388;
    l.client_other_field = 0x400;
    l.session_lo = 4;
    l.session_span = 3;
    l.server_state = 0x110;
    l.vm_static = 0x1008;
    l.gameplay_static = 0x1018;
    l.pause_static = 0x2C8;
    l.client_static = 0x1028;
    l.server_static = 0x1030;
    l.race_flag = 0x1068;
    return l;
}

void Rejects(const char* title, const Faults& faults, const std::string& expect_failure, const Layout& layout = {}) {
    std::printf("%s\n", title);
    const Fixture fx(layout, faults, false);
    OffsetTable got{};
    std::memset(&got, 0xAB, sizeof(got));
    const OffsetTable before = got;
    std::string failure;
    const bool ok = Discover(fx, got, failure);
    std::printf("  failure: %s\n", failure.c_str());
    Check(!ok, "discovery refuses the image");
    Check(failure == expect_failure, "the failure names the check that failed");
    Check(std::memcmp(&got, &before, sizeof(got)) == 0, "the output table is left untouched");
}

void RejectsTruncatedImage() {
    std::printf("A truncated image\n");
    const Fixture fx({}, {}, false);
    std::string failure;
    OffsetTable got{};
    Check(!DiscoverOffsets({ fx.image.data(), 0x2000, fx.PointerBase() }, got, failure),
          "an image cut short of its SizeOfImage is refused");
    std::printf("  failure: %s\n", failure.c_str());
    Check(!DiscoverOffsets({ fx.image.data(), 0x100, fx.PointerBase() }, got, failure),
          "an image cut short of its headers is refused");
    std::vector<std::uint8_t> not_pe(0x1000, 0);
    Check(!DiscoverOffsets({ not_pe.data(), not_pe.size(), 0 }, got, failure), "a buffer with no PE header is refused");
}

void RejectsWrongPointerBase() {
    std::printf("RTTI pointers against the wrong base\n");
    const Fixture fx({}, {}, false);
    std::string failure;
    OffsetTable got{};
    Check(!DiscoverOffsets({ fx.image.data(), fx.image.size(), fx.PointerBase() + 0x10000 }, got, failure),
          "vtables are not found when the image's pointers are relative to another base");
    Check(failure.find("rtti") != std::string::npos, "and the failure names RTTI");
}

// The registry's own selection path, on images laid out in place as a running
// module would be.
void RegistryRoutesUnknownBuildThroughDiscovery() {
    std::printf("The registry on an unlisted fingerprint\n");
    const Fixture good({}, {}, true);
    Check(SelectProfile(const_cast<std::uint8_t*>(good.image.data())) == ProfileSelection::Discovered,
          "a good image selects the discovered values");
    if (Check(HasActiveProfile(), "and leaves an active profile")) {
        CheckTable(ActiveProfile().Offsets, good.expected);
        Check(ActiveProfile().Fingerprint.TimeDateStamp == 0x12345678u, "carrying the running fingerprint");
    }

    Faults broken;
    broken.no_range_check = true;
    const Fixture bad({}, broken, true);
    Check(SelectProfile(const_cast<std::uint8_t*>(bad.image.data())) == ProfileSelection::NoMatch,
          "a broken image afterwards is NoMatch");
    Check(!HasActiveProfile(), "and leaves no active profile behind from the earlier selection");
}

// An image carrying a measured profile's fingerprint. The synthetic structure
// cannot carry that build's values, so discovery on it either fails or finds
// values that differ from the profile.
Layout StampedAs(const BuildProfile& p) {
    Layout l;
    l.time_date_stamp = p.Fingerprint.TimeDateStamp;
    l.image_size = p.Fingerprint.SizeOfImage;
    return l;
}

void RegistryOnAMeasuredFingerprint() {
    std::printf("The registry on a measured profile's fingerprint\n");
    const BuildProfile& p = *kKnownProfiles[0];
    Check(p.Fingerprint.CheckSum == 0, "the newest profile's CheckSum is the zero the fixture writes");

    Faults broken;
    broken.no_range_check = true;
    const Fixture undiscoverable(StampedAs(p), broken, true);
    Check(SelectProfile(const_cast<std::uint8_t*>(undiscoverable.image.data())) == ProfileSelection::Matched,
          "where discovery fails, the measured profile runs");
    if (Check(HasActiveProfile(), "and is active")) {
        Check(&ActiveProfile() == &p, "the active profile is that measured profile");
    }

    const Fixture disagreeing(StampedAs(p), {}, true);
    Check(SelectProfile(const_cast<std::uint8_t*>(disagreeing.image.data())) == ProfileSelection::Inconsistent,
          "where discovery resolves different values, the selection is Inconsistent");
    Check(!HasActiveProfile(), "and leaves the mod dormant with no active profile");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - runtime discovery tests\n");
    std::printf("========================================================\n");

    Resolves("The baseline synthetic image", Layout{});
    Resolves("Every address, static, vtable and field moved", Relocated());
    {
        Faults f;
        f.r9d_before_ctor = true;
        Resolves("mov r9d, imm between the allocation and the constructor", Layout{}, f);
    }
    {
        Faults f;
        f.pause_rax_reused = true;
        Resolves("SetPaused reloading rax and comparing another byte through it", Layout{}, f);
    }
    {
        Faults f;
        f.no_pause_strings = true;
        ResolvesWithoutPause("No function names the pause events", f,
                             "pause: expected one function naming both pause events, found 0 / 0");
    }
    {
        Faults f;
        f.second_pause_function = true;
        ResolvesWithoutPause("Two functions name the pause events", f,
                             "pause: expected one function naming both pause events, found 2 / 2");
    }
    {
        Faults f;
        f.second_tail_jump = true;
        Rejects("Two tail jumps into the view matrix derivation", f,
                "view matrix: expected one tail jump with rcx at the render camera (+0x1350), found 2");
    }
    {
        Faults f;
        f.second_update_caller = true;
        Rejects("Two calls to the view manager update", f, "view manager update: expected one caller, found 2");
    }
    {
        Faults f;
        f.hidden_update_caller = true;
        const Fixture fx({}, {}, false);
        char want[160];
        std::snprintf(want, sizeof(want), "view manager update: 2 rel32 references to 0x%X in .text but 1 decoded",
                      fx.expected.view_manager_update_rva);
        Rejects("A second call to the view manager update after an EVEX instruction", f, want);
    }
    {
        Faults f;
        f.three_cameras = true;
        Rejects("A view manager constructor building three cameras", f,
                "view manager: expected one constructor building four evenly spaced cameras, found 0");
    }
    {
        Faults f;
        f.no_photo_mode_rtti = true;
        Rejects("A controller class missing from the RTTI", f,
                "rtti: expected one primary vtable for .?AVPhotoModeCamera@romu@@, found 0");
    }
    {
        Faults f;
        f.duplicate_vtable = true;
        Rejects("A controller class with two primary vtables", f,
                "rtti: expected one primary vtable for .?AVCarCamera@romu@@, found 2");
    }
    {
        Faults f;
        f.second_camera_ctor = true;
        Rejects("Two functions seeding a camera", f,
                "camera constructor: expected one function seeding near/far/fov, found 2");
    }
    {
        Faults f;
        f.controller_twice = true;
        Rejects("One controller constructed at two offsets", f, [] {
            const Fixture fx({}, {}, false);
            char want[160];
            std::snprintf(want, sizeof(want), "view manager: controller vtable 0x%X constructed at both +0x400 and +0xB00",
                          fx.expected.garage_camera_vtable_rva);
            return std::string(want);
        }());
    }
    {
        Faults f;
        f.second_active_field = true;
        Rejects("Two fields pointed at every controller", f,
                "view manager: expected one field pointed at three or more controllers, found 2");
    }
    {
        Layout l;
        l.active_controller = l.controller_base + 0x10;
        Rejects("The active controller field inside a controller", {},
                "view manager: the active controller field +0x410 is inside a controller", l);
    }
    {
        Faults f;
        f.no_range_check = true;
        Rejects("No session range check on the NetClient state", f,
                "NetClient: expected one session-state range check, found 0");
    }
    {
        Faults f;
        f.second_range_check = true;
        Rejects("Two different session range checks on the NetClient", f,
                "NetClient: expected one session-state range check, found 2");
    }
    {
        Faults f;
        f.second_server_field = true;
        Rejects("Two NetServer state getters reading different fields", f,
                "NetServer: expected one state field across its getters, found 2");
    }
    {
        Faults f;
        f.second_countdown_tick = true;
        Rejects("Two countdown ticks", f, "race phase: expected one countdown tick, found 2");
    }
    {
        Faults f;
        f.no_gameplay_reader = true;
        Rejects("Nothing reads the race phase through the GameplayState static", f,
                "race phase: nothing reads +0x15D0 through the GameplayState static");
    }
    {
        Faults f;
        f.undersized_gameplay_state = true;
        Rejects("A GameplayState allocation too small for the race phase", f,
                "race phase: +0x15D0 / +0x1574 do not fit a GameplayState of 0x15D0 bytes");
    }
    {
        Faults f;
        f.second_gameplay_static = true;
        Rejects("GameplayState stored into two statics", f,
                "GameplayState: expected one static holding the singleton, found 2");
    }
    RejectsTruncatedImage();
    RejectsWrongPointerBase();
    RegistryRoutesUnknownBuildThroughDiscovery();
    RegistryOnAMeasuredFingerprint();

    const char* exes = std::getenv("WF2_GAME_EXES");
    if (exes == nullptr || *exes == '\0') {
        std::printf("No installed game named in WF2_GAME_EXES: the real-image check did not run.\n");
    } else {
        std::string list = exes;
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t end = list.find(';', start);
            const std::string one = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!one.empty()) InstalledImageTest(one);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return wf_test::Summary("runtime discovery");
}
