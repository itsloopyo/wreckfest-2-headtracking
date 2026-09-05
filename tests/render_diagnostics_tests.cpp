// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the render path is allowed to write to the log. Every line here is
// emitted once per camera update in a game running at a hundred frames a
// second, so the edge-triggering and the caps are not a nicety - lose one and
// the log a user is asked to send is megabytes of the same sentence, with the
// two or three lines that explain their problem buried in it.
//
// Each function keeps its own latch in a function-local static, so this drives
// each one through a single designed sequence in one process and counts what
// came out.

#include "render_diagnostics.h"

#include "logging.h"
#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <string>

using wf2_ht::HeadPose;
using wf2_ht::ViewState;
using wf_test::Check;

namespace {

std::wstring g_log_path;

std::wstring MakeLogPath() {
    wchar_t temp[MAX_PATH]{};
    const DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        std::printf("  FAIL: GetTempPathW failed (%lu)\n", GetLastError());
        ++wf_test::g_failures;
        return {};
    }
    return std::wstring(temp, n) + L"wf-ht-diag-test-"
         + std::to_wstring(GetCurrentProcessId()) + L".log";
}

// Everything written since Open(), as one string. The log is opened with
// FILE_SHARE_READ and flushed per line, so this can read it while it is open.
std::string ReadLog() {
    const HANDLE file = CreateFileW(g_log_path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::printf("  FAIL: could not open the log back (%lu)\n", GetLastError());
        ++wf_test::g_failures;
        return {};
    }
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
        text.append(buffer, read);
    }
    CloseHandle(file);
    return text;
}

int CountOccurrences(const std::string& haystack, const char* needle) {
    int found = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1)) {
        ++found;
    }
    return found;
}

// How much of the log this check owns: everything written since the previous
// check finished, so one function's lines cannot be counted against another's.
std::size_t g_read_from = 0;

std::string NewLines() {
    const std::string all = ReadLog();
    const std::string fresh = all.size() >= g_read_from ? all.substr(g_read_from) : all;
    g_read_from = all.size();
    return fresh;
}

void ConnectionLocalityTests() {
    std::printf("The tracker's source is logged on change, not per frame\n");
    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogConnectionLocality(false, 0.0f, 0.15f);
    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogConnectionLocality(true, 0.0f, 0.15f);
    wf2_ht::diag::LogConnectionLocality(false, 0.0f, 0.15f);

    const std::string log = NewLines();
    Check(CountOccurrences(log, "tracker source is") == 3,
          "three lines for three distinct states, not eleven");
    Check(CountOccurrences(log, "tracker source is on this machine") == 2, "local, twice");
    Check(CountOccurrences(log, "tracker source is a remote device") == 1, "remote, once");
    Check(log.find("smoothing=0.15") != std::string::npos,
          "the remote line reports the remote value");
    Check(log.find("smoothing=0.00") != std::string::npos,
          "the local line reports the local value");
}

void TrackerPresenceTests() {
    std::printf("Tracker comings and goings are edge-triggered and capped\n");
    // Starts believing nothing is arriving, so a run of "no data" says nothing.
    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogTrackerPresence(false);
    Check(NewLines().empty(), "no data, and nothing said about it");

    // Ten changes are reported; the eleventh and everything after it is not.
    for (int i = 0; i < 40; ++i) wf2_ht::diag::LogTrackerPresence(i % 2 == 0);

    const std::string log = NewLines();
    Check(CountOccurrences(log, "tracker data is arriving")
              + CountOccurrences(log, "tracker data stopped arriving") == 10,
          "ten changes logged out of forty");
    Check(CountOccurrences(log, "further changes are not logged") == 1,
          "and one line saying the rest will not be");
}

void GateChangeTests() {
    std::printf("The gate logs when one of its inputs changes\n");
    for (int i = 0; i < 4; ++i) {
        wf2_ht::diag::LogGateChange(true, ViewState::Gameplay, true, false, true);
    }
    wf2_ht::diag::LogGateChange(true, ViewState::Gameplay, true, true, false);
    wf2_ht::diag::LogGateChange(true, ViewState::Menu, true, true, false);

    // The player's own toggle is an input to the gate like any other. Before it
    // was compared here, pressing End mid-race flipped the answer with no line,
    // and the last [state] line in the log went on saying "following".
    wf2_ht::diag::LogGateChange(false, ViewState::Menu, true, true, false);

    const std::string log = NewLines();
    Check(CountOccurrences(log, "head tracking") == 4, "one line per distinct combination");
    Check(CountOccurrences(log, "view is gameplay") == 2, "named by view state");
    Check(CountOccurrences(log, "not paused") == 1, "and by each flag behind the answer");
    Check(CountOccurrences(log, "tracking is switched off") == 1,
          "including the player's own toggle");
}

void FirstFramesTests() {
    std::printf("The camera transform is dumped for the first few frames only\n");
    float transform[wf2_ht::kCameraTransformFloats]{};
    transform[0] = 1.0f;
    transform[5] = 1.0f;
    transform[10] = 1.0f;
    transform[15] = 1.0f;
    const HeadPose pose;

    for (long long frame = 0; frame < 20; ++frame) {
        wf2_ht::diag::LogFirstFrames(frame, transform, false, pose);
    }

    const std::string log = NewLines();
    Check(CountOccurrences(log, "tracker=no") == 3, "three frames, out of twenty");
    Check(CountOccurrences(log, "[camera]   m[") == 12, "four matrix rows for each of them");
}

void FirstPoseReachingCameraTests() {
    std::printf("The first pose to reach the hook is announced once\n");
    HeadPose pose;
    pose.yaw = 1.0f;

    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogFirstPoseReachingCamera(i, false, pose);
    Check(NewLines().empty(), "nothing while there is no tracker rotation");

    for (int i = 5; i < 10; ++i) wf2_ht::diag::LogFirstPoseReachingCamera(i, true, pose);
    const std::string log = NewLines();
    Check(CountOccurrences(log, "head pose reached the camera hook") == 1, "said once");
    Check(log.find("on frame 5") != std::string::npos, "naming the frame it first arrived on");
}

void FirstComposedPoseTests() {
    std::printf("The first pose big enough to see composed into a frame is announced once\n");
    HeadPose pose;
    pose.yaw = 1.0f;
    pose.pitch = -2.0f;
    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogFirstComposedPose(pose);
    Check(NewLines().empty(), "a head sitting still is tracker jitter, not a turn");

    pose.yaw = 12.0f;
    for (int i = 0; i < 5; ++i) wf2_ht::diag::LogFirstComposedPose(pose);
    const std::string log = NewLines();
    Check(CountOccurrences(log, "composed a head pose into the frame") == 1,
          "said once, on the first deliberate movement");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - render diagnostics tests\n");
    std::printf("=======================================================\n");

    g_log_path = MakeLogPath();
    if (g_log_path.empty()) return wf_test::Summary("render diagnostics");
    wf2_ht::Log::Open(g_log_path);

    ConnectionLocalityTests();
    TrackerPresenceTests();
    GateChangeTests();
    FirstFramesTests();
    FirstPoseReachingCameraTests();
    FirstComposedPoseTests();

    wf2_ht::Log::Close();
    DeleteFileW(g_log_path.c_str());
    DeleteFileW(cameraunlock::logging::PreviousGenerationPath(g_log_path).c_str());
    return wf_test::Summary("render diagnostics");
}
