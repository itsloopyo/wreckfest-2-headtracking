// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace wf2_ht {

// Places both halves of the camera sandwich, at the RVAs the active build
// profile pins.
//
// The head pose is composed into the render camera's world transform just
// before the engine derives this frame's view matrix from it, and left there
// for the rest of the frame - the renderer reads the camera again, well after
// the derivation returns, so an injection that lived only for the duration of
// that call reached the frame's motion vectors and nothing else. The view
// manager's own camera update takes the pose back out at the top of the next
// frame, so the engine never interpolates from a transform the head moved.
//
// Returns false when the profile's camera transform is not the shape this mod
// composes, or when MinHook could not place either detour. Nothing is left
// patched in either case.
bool InstallCameraHook();

}  // namespace wf2_ht
