// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "render_diagnostics.h"

#include <cmath>

#include "logging.h"

#include "cameraunlock/math/smoothing_utils.h"

namespace wf2_ht::diag {

namespace {

// How many of the first camera updates dump the transform in full.
constexpr long long kDiagnosticFrames = 3;

// How many times the tracker may come and go before the log stops saying so.
constexpr int kTrackerPresenceChanges = 10;

// How far off centre a pose has to be before it counts as the player having
// deliberately moved, rather than the jitter a tracker emits sitting still.
constexpr float kDeliberateMovementDegrees = 5.0f;

}  // namespace

void LogConnectionLocality(bool is_remote, float local_smoothing, float remote_smoothing) {
    static bool last_remote = false;
    static bool known = false;

    if (known && is_remote == last_remote) return;
    last_remote = is_remote;
    known = true;

    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              is_remote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(local_smoothing, remote_smoothing,
                                                        is_remote));
}

void LogTrackerPresence(bool receiving) {
    static bool last_receiving = false;
    static int changes = 0;

    if (receiving == last_receiving) return;
    last_receiving = receiving;

    if (changes >= kTrackerPresenceChanges) return;
    ++changes;

    Log::Line("[udp] tracker data %s", receiving
                  ? "is arriving"
                  : "stopped arriving - the last pose is held until it resumes");
    if (changes == kTrackerPresenceChanges) {
        Log::Line("[udp] the tracker has come and gone %d times; further changes are not logged.",
                  kTrackerPresenceChanges);
    }
}

void LogGateChange(bool tracking_enabled, ViewState view, bool gameplay_running, bool paused,
                   bool following) {
    static bool last_tracking_enabled = false;
    static ViewState last_view = ViewState::Unknown;
    static bool last_gameplay_running = false;
    static bool last_paused = false;
    static bool known = false;

    if (known && tracking_enabled == last_tracking_enabled && view == last_view
        && gameplay_running == last_gameplay_running && paused == last_paused) {
        return;
    }
    last_tracking_enabled = tracking_enabled;
    last_view = view;
    last_gameplay_running = gameplay_running;
    last_paused = paused;
    known = true;

    Log::Line("[state] tracking is switched %s, view is %s, gameplay %s, %s - head tracking %s",
              tracking_enabled ? "on" : "off",
              ViewStateName(view), gameplay_running ? "running" : "not running",
              paused ? "paused" : "not paused",
              following ? "following" : "off");
}

void LogFirstFrames(long long frame, const float* transform, bool have_rotation,
                    const HeadPose& pose) {
    if (frame >= kDiagnosticFrames) return;

    Log::Line("[camera] frame %lld  tracker=%s  yaw=%.2f pitch=%.2f roll=%.2f  lean=%.3f %.3f %.3f",
              frame, have_rotation ? "yes" : "no", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
    for (unsigned row = 0; row < kCameraTransformFloats / 4; ++row) {
        Log::Line("[camera]   m[%u] % 14.5f % 14.5f % 14.5f % 14.5f", row,
                  transform[row * 4 + 0], transform[row * 4 + 1],
                  transform[row * 4 + 2], transform[row * 4 + 3]);
    }
}

void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose) {
    static bool logged = false;
    if (!have_rotation) return;
    if (logged) return;
    logged = true;

    Log::Line("[camera] head pose reached the camera hook on frame %lld: "
              "yaw=%.2f pitch=%.2f roll=%.2f", frame, pose.yaw, pose.pitch, pose.roll);
}

void LogFirstComposedPose(const HeadPose& pose) {
    // Plain, like every other latch in this file: everything here is called
    // only from the camera detour, which runs on the one thread that drives the
    // view manager's camera update (see camera_hook.cpp).
    static bool logged = false;
    if (logged) return;
    if (std::fabs(pose.yaw) < kDeliberateMovementDegrees
        && std::fabs(pose.pitch) < kDeliberateMovementDegrees) {
        return;
    }
    logged = true;
    Log::Line("[camera] composed a head pose into the frame: yaw=%.2f pitch=%.2f roll=%.2f "
              "lean=%.3f %.3f %.3f", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
}

}  // namespace wf2_ht::diag
