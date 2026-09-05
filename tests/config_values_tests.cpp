// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What HeadTracking.ini does with a value the mod cannot use, and - the half
// that had no cover at all - whether it SAYS so.
//
// The value side was already right: every reader in IniReader answers
// unparseable text with the fallback its caller passed, and the loader passes
// the value the Config already holds, so a refused key keeps what it had. What
// was missing was the diagnostic. A refused value looked exactly like a key the
// user never wrote, so "my INI setting is ignored" arrived with an empty log
// and nothing to triage from, while every other refusal in config.cpp - a bad
// port, a bad hotkey, an out-of-range float - reported itself.
//
// The trap is a bool with a trailing comment. GetPrivateProfileString does not
// treat ';' as a comment introducer and ReadBool matches the WHOLE value, so
// `Enabled=0 ; no lean` matches nothing and position tracking stays on.
//
// The other half of this file is the regression risk the fix carries: the
// detector must not cry wolf on values that are perfectly legitimate.

#include "config.h"
#include "logging.h"

#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <share.h>
#include <string>

using namespace wf2_ht;
using wf_test::Check;
using wf_test::CheckClose;

namespace {

std::string g_dir;

std::string IniPath() {
    return g_dir + "\\HeadTracking.ini";
}

std::string LogPath() {
    return g_dir + "\\HeadTracking.log";
}

bool MakeTempDir() {
    char temp[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        std::printf("  FAIL: GetTempPathA failed (%lu)\n", GetLastError());
        ++wf_test::g_failures;
        return false;
    }
    g_dir = std::string(temp, n) + "wf-ht-values-test-"
          + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(g_dir.c_str(), nullptr);

    // The log config.cpp writes its refusals to. Opened with FILE_SHARE_READ,
    // so it can be read back while it is still open.
    //
    // Widened through the API rather than by copying chars: a temp path under a
    // non-ASCII user name would otherwise reach Log::Open as a different path,
    // the log would land somewhere else, and every check below would fail with
    // nothing to say why.
    const std::string path = LogPath();
    const int wide_length = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (wide_length <= 0) {
        std::printf("  FAIL: could not widen %s\n", path.c_str());
        ++wf_test::g_failures;
        return false;
    }
    std::wstring wide(static_cast<size_t>(wide_length) - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wide[0], wide_length);
    Log::Open(wide);
    return true;
}

void RemoveTempDir() {
    Log::Close();
    DeleteFileA(IniPath().c_str());
    DeleteFileA(LogPath().c_str());
    DeleteFileA((g_dir + "\\HeadTracking.prev.log").c_str());
    RemoveDirectoryA(g_dir.c_str());
}

std::string ReadWholeLog() {
    // _fsopen, not fopen_s: fopen_s denies sharing by default, so it cannot open
    // a file the log still holds a write handle on and every read came back
    // empty - which reads as "nothing was logged" and quietly passes every
    // check that asserts silence.
    FILE* f = _fsopen(LogPath().c_str(), "rb", _SH_DENYNO);
    if (f == nullptr) return {};
    std::string text;
    char chunk[4096];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) text.append(chunk, got);
    std::fclose(f);
    return text;
}

// A Config loaded from `body`, plus only the log lines that load produced.
struct Load {
    Config config;
    std::string log;
};

Load LoadIni(const char* body) {
    const size_t logBefore = ReadWholeLog().size();

    FILE* f = nullptr;
    fopen_s(&f, IniPath().c_str(), "w");
    if (!f) {
        std::printf("  FAIL: could not write %s\n", IniPath().c_str());
        ++wf_test::g_failures;
        return {};
    }
    std::fputs(body, f);
    std::fclose(f);

    Load result;
    LoadConfig(g_dir, result.config);
    result.log = ReadWholeLog().substr(logBefore);
    return result;
}

bool Mentions(const std::string& log, const char* key) {
    return log.find(key) != std::string::npos;
}

// Both halves of one refusal, checked together: the value must not move, and
// the log must name the key. Either alone is the bug.
void CheckRefusedAndReported(const Load& loaded, bool actual, bool expected, const char* key) {
    Check(actual == expected, "the flag keeps its previous value");
    Check(Mentions(loaded.log, key), "and the log names the key that was refused");
}

void CheckRefusedAndReported(const Load& loaded, float actual, float expected, const char* key) {
    CheckClose(actual, expected, "the value keeps its previous value");
    Check(Mentions(loaded.log, key), "and the log names the key that was refused");
}

void AcceptedValuesAreReadAndNotComplainedAbout() {
    // The regression the refusal detector could introduce: a legitimate value
    // reported as unreadable, so the log fills with complaints about settings
    // that are working. Every check here is a value the mod must take silently.
    std::printf("A value the mod can use is taken, and taken quietly\n");

    const Load off = LoadIni("[Position]\nEnabled=0\n");
    Check(off.config.position_enabled == false, "0 turns position tracking off");
    Check(!Mentions(off.log, "Enabled"), "and nothing is said about it");

    const Load word = LoadIni("[Position]\nEnabled=false\n");
    Check(word.config.position_enabled == false, "false turns it off too");
    Check(!Mentions(word.log, "Enabled"), "and nothing is said about that either");

    const Load on = LoadIni("[General]\nEnableOnStartup=off\n");
    Check(on.config.enable_on_startup == false, "off is a spelling of 0");
    Check(!Mentions(on.log, "EnableOnStartup"), "and is taken silently");

    // A trailing comment is taken off before the number is parsed, so this
    // must go on working rather than be swept up by the whole-token refusal.
    const Load commented = LoadIni("[Position]\nLimitX=0.25 ; tighter\n");
    CheckClose(commented.config.limit_x, 0.25f,
               "a number with a trailing comment is still read");
    Check(!Mentions(commented.log, "LimitX"), "and is still taken silently");

    const Load zero = LoadIni("[Position]\nLimitY=0\n");
    CheckClose(zero.config.limit_y, 0.0f, "a zero limit (axis pinned) is a real setting");
    Check(!Mentions(zero.log, "LimitY"), "and is taken silently");

    const Load smoothing = LoadIni("[Rotation]\nLocalSmoothing=0\n");
    CheckClose(smoothing.config.local_smoothing, 0.0f,
               "a configured zero smoothing is a real setting");
    Check(!Mentions(smoothing.log, "LocalSmoothing"), "and is taken silently");

    const Load port = LoadIni("[Network]\nUdpPort=5005\n");
    Check(port.config.udp_port == 5005, "an in-range port is read");
    Check(!Mentions(port.log, "UdpPort"), "and is taken silently");
}

void RefusedValuesAreReported() {
    std::printf("A value the mod cannot use is kept off, and said out loud\n");
    const Config defaults;

    // The one this exists for.
    const Load commented = LoadIni("[Position]\nEnabled=0 ; no lean\n");
    CheckRefusedAndReported(commented, commented.config.position_enabled,
                            defaults.position_enabled, "Enabled");

    const Load prose = LoadIni("[General]\nEnableOnStartup=yes please\n");
    CheckRefusedAndReported(prose, prose.config.enable_on_startup,
                            defaults.enable_on_startup, "EnableOnStartup");

    // A decimal comma is a PREFIX parse away from being accepted as 0.0, which
    // is in range and would be taken in silence - and on RemoteSmoothing that
    // is exactly the un-smoothed phone on WiFi the key exists to prevent.
    const Load comma = LoadIni("[Rotation]\nRemoteSmoothing=0,15\n");
    CheckRefusedAndReported(comma, comma.config.remote_smoothing, 0.15f, "RemoteSmoothing");

    const Load commaLimit = LoadIni("[Position]\nLimitX=0,30\n");
    CheckRefusedAndReported(commaLimit, commaLimit.config.limit_x, defaults.limit_x, "LimitX");

    const Load trailing = LoadIni("[Position]\nLimitY=0.2 metres\n");
    CheckRefusedAndReported(trailing, trailing.config.limit_y, defaults.limit_y, "LimitY");

    const Load limit = LoadIni("[Position]\nLimitX=wide\n");
    CheckRefusedAndReported(limit, limit.config.limit_x, defaults.limit_x, "LimitX");

    // A malformed RemoteSmoothing must never land on the LOCAL default: that
    // would hand a phone on WiFi no smoothing at all on raw network jitter.
    const Load remote = LoadIni("[Rotation]\nRemoteSmoothing=nonsense\n");
    CheckRefusedAndReported(remote, remote.config.remote_smoothing, 0.15f, "RemoteSmoothing");
}

void AbsentKeysAreSilent() {
    // The other side of the same coin: a key the user never wrote is not a
    // refusal, and reporting it would bury the ones that are.
    std::printf("A key the file never mentions says nothing\n");
    const Config defaults;

    const Load empty = LoadIni("[Network]\nUdpPort=4242\n");
    Check(empty.config.position_enabled == defaults.position_enabled,
          "an unmentioned flag keeps its default");
    Check(!Mentions(empty.log, "Enabled"), "and is not reported as refused");
    Check(!Mentions(empty.log, "LimitZ"), "nor is an unmentioned limit");

    const Load blank = LoadIni("[General]\nEnableOnStartup=\n");
    Check(blank.config.enable_on_startup == defaults.enable_on_startup,
          "a key with an empty value keeps its default");
    Check(!Mentions(blank.log, "EnableOnStartup"),
          "and an empty value is treated as unset, not as garbage");
}

void BoundaryChecksStillRun() {
    // The refusal sits in front of the range checks, not instead of them.
    std::printf("What does parse still goes through the boundary checks\n");
    const Config defaults;

    CheckClose(LoadIni("[Rotation]\nRemoteSmoothing=5.0\n").config.remote_smoothing, 1.0f,
               "a smoothing above 1 still clamps");
    CheckClose(LoadIni("[Position]\nLimitY=-3\n").config.limit_y, 0.0f,
               "a negative limit still clamps to zero");
    CheckClose(LoadIni("[Position]\nLimitZBack=1e400\n").config.limit_z_back,
               defaults.limit_z_back,
               "a value that overflows to infinity still falls back");
    CheckClose(LoadIni("[Position]\nLimitZ=4.0\n").config.limit_z, 0.5f,
               "a misplaced decimal point still clamps to the documented maximum");

    // 70000 & 0xFFFF is 4464 - a port the mod would bind and then report as the
    // one that was asked for.
    Check(LoadIni("[Network]\nUdpPort=70000\n").config.udp_port == defaults.udp_port,
          "a port above 65535 falls back rather than truncating to 4464");
    Check(LoadIni("[Network]\nUdpPort=80\n").config.udp_port == defaults.udp_port,
          "a privileged port falls back");
    Check(LoadIni("[Network]\nUdpPort=abc\n").config.udp_port == defaults.udp_port,
          "unreadable text falls back rather than binding port 0");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - config value tests\n");
    std::printf("===================================================\n");
    if (!MakeTempDir()) return wf_test::Summary("config values");
    AcceptedValuesAreReadAndNotComplainedAbout();
    RefusedValuesAreReported();
    AbsentKeysAreSilent();
    BoundaryChecksStillRun();
    RemoveTempDir();
    return wf_test::Summary("config values");
}
