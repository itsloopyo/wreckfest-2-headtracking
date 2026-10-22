// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Runtime discovery, two ways. Synthetic images built by discovery_fixture.h
// prove the resolver follows the structure wherever it is laid out and refuses
// an image with any piece of it missing or doubled. Every installed
// Wreckfest2.exe named in WF2_GAME_EXES (semicolon separated; run-tests.ps1
// fills it from core's game path detection) is then mapped the way the loader
// maps it and resolved from scratch; where its fingerprint matches a measured
// profile, every field has to agree with that profile.

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

struct MappedImage {
    std::vector<std::uint8_t> bytes;
    std::uint64_t image_base = 0;
    cameraunlock::memory::PeFingerprint fingerprint{};
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
    if (known == nullptr) std::printf("  no measured profile has this fingerprint; values printed above\n");
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

void ResolvesLayout(const char* title, const Layout& layout) {
    std::printf("%s\n", title);
    const Fixture fx(layout, {}, false);
    OffsetTable got{};
    std::string failure;
    const bool ok = Discover(fx, got, failure);
    if (!ok) std::printf("  discovery failed: %s\n", failure.c_str());
    if (!Check(ok, "discovery resolves every value")) return;
    CheckTable(got, fx.expected);
}

// A layout that moves every function, static and field the resolver reports.
Layout Relocated() {
    Layout l;
    l.pad = 0x40;
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

void Rejects(const char* title, const Faults& faults, const char* expect_in_failure) {
    std::printf("%s\n", title);
    const Fixture fx({}, faults, false);
    OffsetTable got{};
    std::memset(&got, 0xAB, sizeof(got));
    const OffsetTable before = got;
    std::string failure;
    const bool ok = Discover(fx, got, failure);
    std::printf("  failure: %s\n", failure.c_str());
    Check(!ok, "discovery refuses the image");
    Check(failure.find(expect_in_failure) != std::string::npos, "the failure names the check that failed");
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

// The registry's own selection path, on an image whose fingerprint no measured
// profile carries, laid out in place as a running module would be.
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
    broken.no_pause_strings = true;
    const Fixture bad({}, broken, true);
    Check(SelectProfile(const_cast<std::uint8_t*>(bad.image.data())) == ProfileSelection::NoMatch,
          "a broken image afterwards is NoMatch");
    Check(!HasActiveProfile(), "and leaves no active profile behind from the earlier selection");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - runtime discovery tests\n");
    std::printf("========================================================\n");

    ResolvesLayout("The baseline synthetic image", Layout{});
    ResolvesLayout("Every address, static and field moved", Relocated());
    {
        Faults f;
        f.no_pause_strings = true;
        Rejects("No function names the pause events", f, "pause");
    }
    {
        Faults f;
        f.second_tail_jump = true;
        Rejects("Two tail jumps into the view matrix derivation", f, "view matrix");
    }
    {
        Faults f;
        f.second_update_caller = true;
        Rejects("Two calls to the view manager update", f, "view manager update");
    }
    {
        Faults f;
        f.three_cameras = true;
        Rejects("A view manager constructor building three cameras", f, "view manager");
    }
    {
        Faults f;
        f.no_photo_mode_rtti = true;
        Rejects("A controller class missing from the RTTI", f, "PhotoModeCamera");
    }
    {
        Faults f;
        f.no_range_check = true;
        Rejects("No session range check on the NetClient state", f, "NetClient");
    }
    {
        Faults f;
        f.no_gameplay_reader = true;
        Rejects("Nothing reads the race phase through the GameplayState static", f, "race phase");
    }
    {
        Faults f;
        f.undersized_gameplay_state = true;
        Rejects("A GameplayState allocation too small for the race phase", f, "do not fit");
    }
    {
        Faults f;
        f.second_gameplay_static = true;
        Rejects("GameplayState stored into two statics", f, "GameplayState");
    }
    RejectsTruncatedImage();
    RejectsWrongPointerBase();
    RegistryRoutesUnknownBuildThroughDiscovery();

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
