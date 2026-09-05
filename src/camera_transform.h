// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace wf2_ht {

// The camera transform this mod reads and writes back is a full 4x4. Nothing
// discovers that at runtime - it comes from the build profile - so the hook
// installer checks the active profile against it and refuses to patch on a
// mismatch, rather than letting the per-frame path compose 16 floats into a
// shorter transform and run off the end of the engine's output struct.
constexpr unsigned kCameraTransformFloats = 16;

// A processed head pose as the core pipeline hands it over: degrees of
// rotation in OpenTrack's convention, and metres of lean in the position
// processor's, where negative z is a lean forward. Every axis the engine's own
// convention differs on - its pitch, its lateral axis and its forward axis all
// run opposite - is negated inside ApplyHeadPose. Yaw matches and is passed
// through.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float lean_z = 0.0f;
};

// Composes `pose` into the engine's freshly computed camera-to-world transform,
// in place. `transform` is row-major with the camera position in elements
// 12..14, which is the convention the whole of camera_transform.cpp encodes.
//
// All three rotation axes are camera-local: head yaw turns about the CAMERA's
// own up axis, not the world's. A car in Wreckfest 2 spends real time banked,
// airborne and upside down, and a world-locked yaw axis in those moments turns
// the view about something with no relation to where the driver is looking.
// The camera's position is untouched, so the view never orbits the car.
//
// Pure: no engine state, no logging, no clock. The hook decides whether to
// call it; this decides only what the matrix becomes.
void ApplyHeadPose(float transform[kCameraTransformFloats], const HeadPose& pose);

}  // namespace wf2_ht
