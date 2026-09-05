// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace wf2_ht {

// Where a window `window_extent` long starts when it is centred in an area that
// starts at `area_start` and is `area_extent` long, clamped so it never starts
// before the area does.
//
// The clamp is for the window this game actually produces: at the resolutions
// people play at, the frame and title bar make the outer window taller than the
// desktop work area, and the arithmetic centre of something that does not fit
// is above the top of the screen. A title bar off the top of the screen cannot
// be grabbed, so the window can no longer be dragged anywhere.
int CenteredWindowOrigin(int area_start, int area_extent, int window_extent);

// Whether a window that starts at `origin` counts as already centred when the
// centre is `centred_origin`.
//
// Not an exact comparison: this game places its own window on the arithmetic
// centre with an odd remainder rounded up, where CenteredWindowOrigin rounds it
// down, so a window that is already where it should be reads as a pixel out.
bool IsAlreadyCentred(int origin, int centred_origin);

// Waits for the engine to create its window, then centres it on the work area
// of the monitor it is on. Called once, from the bootstrap thread; it blocks
// until the window exists, so nothing else in the bootstrap runs behind it.
//
// A window with no title bar, and a window that already covers its whole
// monitor, are both exclusive fullscreen or borderless - centred by definition -
// so only the windowed case moves.
void CenterGameWindowWhenItExists();

}  // namespace wf2_ht
