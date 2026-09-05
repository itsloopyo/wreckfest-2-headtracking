// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The defaults a user gets exist in three places: the Config struct's member
// initialisers (what the mod falls back to), the default HeadTracking.ini the
// mod writes on first run, and the reference HeadTracking.ini committed at the
// repo root. Nothing forces them to agree, and a drift between them is silent -
// the mod would behave one way and the documented file would say another.
//
// This locks all three together by round-tripping the real generator and the
// real loader.

#include "config.h"

#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace wf2_ht;
using wf_test::Check;
using wf_test::CheckClose;

namespace {

std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

std::string MakeTempDir() {
    char temp[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        std::printf("  FAIL: GetTempPathA failed (%lu)\n", GetLastError());
        ++wf_test::g_failures;
        return {};
    }
    std::string dir = std::string(temp, n) + "wf-ht-config-test-"
                    + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

void RemoveTempDir(const std::string& dir) {
    DeleteFileA((dir + "\\HeadTracking.ini").c_str());
    RemoveDirectoryA(dir.c_str());
}

// Every field of Config, compared against a default-constructed one. Loading a
// file that says exactly what the defaults say must leave the struct untouched.
void CheckMatchesDefaults(const Config& cfg, const char* source) {
    const Config defaults;
    std::printf("%s\n", source);

    Check(cfg.udp_port == defaults.udp_port, "UdpPort");
    Check(cfg.enable_on_startup == defaults.enable_on_startup, "EnableOnStartup");

    Check(cfg.toggle_key == defaults.toggle_key, "ToggleKey");
    Check(cfg.cycle_mode_key == defaults.cycle_mode_key, "CycleModeKey");
    Check(cfg.chord_toggle_key == defaults.chord_toggle_key, "ChordToggleKey");
    Check(cfg.chord_cycle_mode_key == defaults.chord_cycle_mode_key, "ChordCycleModeKey");

    CheckClose(cfg.local_smoothing, defaults.local_smoothing, "LocalSmoothing");
    CheckClose(cfg.remote_smoothing, defaults.remote_smoothing, "RemoteSmoothing");

    Check(cfg.position_enabled == defaults.position_enabled, "Position Enabled");
    CheckClose(cfg.limit_x, defaults.limit_x, "LimitX");
    CheckClose(cfg.limit_y, defaults.limit_y, "LimitY");
    CheckClose(cfg.limit_z, defaults.limit_z, "LimitZ");
    CheckClose(cfg.limit_z_back, defaults.limit_z_back, "LimitZBack");
}

// Every field set to something no default is, so a key the loader never saw
// leaves its poison behind and CheckMatchesDefaults catches it. Shared by both
// tests below: poisoning only some fields in one of them made that test compare
// default against default for the rest, so a key missing from the file under
// test would have passed.
Config Poisoned() {
    Config cfg;
    cfg.udp_port = 5555;
    cfg.enable_on_startup = false;
    cfg.toggle_key = 0x70;
    cfg.cycle_mode_key = 0x71;
    cfg.chord_toggle_key = 0x72;
    cfg.chord_cycle_mode_key = 0x73;
    cfg.local_smoothing = 0.99f;
    cfg.remote_smoothing = 0.99f;
    cfg.position_enabled = false;
    cfg.limit_x = cfg.limit_y = cfg.limit_z = cfg.limit_z_back = 0.49f;
    return cfg;
}

// The reference file at the repo root has to BE the file the mod writes, not
// merely agree with it about values: it is what the README documents and what a
// user compares their own edited copy against, and its comments are half of
// what it is for. A value-only check let three comment lines drift in and out
// unnoticed, so this compares the bytes.
void ReferenceIniIsTheGeneratedFileTest(const std::string& generated_dir) {
    std::printf("The reference HeadTracking.ini at the repo root\n");

    const std::string generated = ReadWholeFile(generated_dir + "\\HeadTracking.ini");
    const std::string reference =
        ReadWholeFile(std::string(WF2_SOURCE_DIR) + "\\HeadTracking.ini");
    if (!Check(!generated.empty(), "the generated file could be read back")) return;
    if (!Check(!reference.empty(), "the reference file could be read")) return;

    if (!Check(generated == reference,
               "is byte-identical to the file the mod writes on first run")) {
        std::printf("  (generated %zu bytes, reference %zu bytes - regenerate the reference "
                    "from kDefaultIniText in src/config.cpp)\n",
                    generated.size(), reference.size());
    }
}

void GeneratedDefaultsTests() {
    const std::string dir = MakeTempDir();
    if (dir.empty()) return;

    WriteDefaultConfigIfMissing(dir);

    const std::string path = dir + "\\HeadTracking.ini";
    std::printf("The generated default HeadTracking.ini\n");
    Check(GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES,
          "is written when none exists");

    Config cfg = Poisoned();
    LoadConfig(dir, cfg);
    CheckMatchesDefaults(cfg, "The generated file loads back as the built-in defaults");

    ReferenceIniIsTheGeneratedFileTest(dir);

    // A second call must not clobber a file the user has since edited.
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    Check(f != nullptr, "the test can rewrite the file");
    if (f) {
        std::fputs("[Network]\nUdpPort=5000\n", f);
        std::fclose(f);
    }
    WriteDefaultConfigIfMissing(dir);
    Config edited;
    LoadConfig(dir, edited);
    Check(edited.udp_port == 5000, "an existing HeadTracking.ini is never overwritten");

    RemoveTempDir(dir);
}

void ReferenceIniTests() {
    // WF2_SOURCE_DIR is the repo root, where the reference HeadTracking.ini
    // that ships as documentation lives.
    Config cfg = Poisoned();
    LoadConfig(WF2_SOURCE_DIR, cfg);
    CheckMatchesDefaults(cfg, "The reference HeadTracking.ini at the repo root");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - config default tests\n");
    std::printf("=====================================================\n");
    GeneratedDefaultsTests();
    ReferenceIniTests();
    return wf_test::Summary("config defaults");
}
