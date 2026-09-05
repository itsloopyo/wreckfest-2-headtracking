// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace wf2_ht {

void Initialize();

// Called from the camera detour with the engine's camera-to-world transform,
// immediately before the engine derives this frame's view matrix from it.
// `transform` is kCameraTransformFloats long (see camera_transform.h);
// InstallCameraHook refuses to patch a build whose profile says otherwise.
//
// Returns true if the transform was modified, in which case the hook remembers
// which camera it was and puts the engine's own transform back at the top of
// the next view manager update - before anything interpolates from it.
bool ApplyTrackingToCameraTransform(float* transform);

}  // namespace wf2_ht
