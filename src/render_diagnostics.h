// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "camera_transform.h"
#include "game_state.h"

// Everything the mod writes to the log from the render path.
//
// Every function here is edge-triggered or bounded, because this runs once per
// camera update and a line per frame is a log nobody can read. Each takes what
// it reports as arguments rather than reading the mod's pipeline state, so the
// coupling is on the call site where it can be seen, and what a given sequence
// of frames writes can be checked without a game running.
namespace wf2_ht::diag {

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// This only records the switch, so a bug report can say which of the two values
// was actually in effect.
void LogConnectionLocality(bool is_remote, float local_smoothing, float remote_smoothing);

// Edge-triggered, and capped. Nothing else in the log says the tracker went
// quiet: the receiver's first-packet line fires once and never again, and the
// pipeline holds the last pose rather than reporting the gap, so "the view
// froze" and "the mod never saw the tracker" read identically. The cap is there
// because a tracker that keeps losing the face can flap this every second, and
// a log that is mostly flapping is a log nobody can read.
void LogTrackerPresence(bool receiving);

// One line whenever any input to the gate changes, naming what each of them
// said and what the gate made of it. `tracking_enabled` is one of those inputs:
// without it, pressing End mid-race flips the gate with no gate line, and the
// last [state] line in the log goes on claiming head tracking is following.
void LogGateChange(bool tracking_enabled, ViewState view, bool gameplay_running, bool paused,
                   bool following);

// Enough of the engine's camera transform to confirm in a bug report that the
// hook fires and that the matrix still looks like a camera-to-world transform
// (row 3 holding world-scale translation) after a game patch. Bounded to the
// first few frames because this runs on the render path.
void LogFirstFrames(long long frame, const float* transform, bool have_rotation,
                    const HeadPose& pose);

// Latched, and reported ahead of the gate. The diagnostic frames above are
// logged the moment the hook first runs, which is long before a tracker is
// usually connected, so without this nothing in the log ever says the head pose
// reached the camera. Behind the gate instead, one press of End - or one lap
// spent in the garage - would hide the evidence.
void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose);

// Once, for the first pose that both cleared the gate and was big enough to see.
// LogFirstPoseReachingCamera above fires on the first packet to arrive, which is
// normally a near-centred one - it says the data got here, not that the view
// ever turned. This is the line that separates "the tracker is not reaching the
// camera" from "the camera is not reaching the screen", which is the whole of
// triaging a report that head tracking does nothing.
void LogFirstComposedPose(const HeadPose& pose);

}  // namespace wf2_ht::diag
