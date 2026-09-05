// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where a centred game window starts. Moving the window needs a desktop and a
// running game; deciding the position it moves to does not, which is the part
// locked here - including the clamp for a window larger than the work area,
// the case this game produces at the resolutions it is played at.

#include "game_window.h"

#include "test_support.h"

#include <cstdio>

using wf2_ht::CenteredWindowOrigin;
using wf2_ht::IsAlreadyCentred;
using wf_test::Check;

namespace {

void FittingWindowTests() {
    std::printf("A window that fits is centred in the area\n");
    Check(CenteredWindowOrigin(0, 1920, 1280) == 320, "1280 wide in 1920 starts at 320");
    Check(CenteredWindowOrigin(0, 1920, 1920) == 0, "a window the width of the area starts at 0");
    Check(CenteredWindowOrigin(0, 1000, 501) == 249,
          "an odd remainder rounds towards the area start");
}

// A second monitor to the right, or a work area that starts below a top-docked
// taskbar, both put the area somewhere other than the origin.
void OffsetAreaTests() {
    std::printf("The area's own start is carried through\n");
    Check(CenteredWindowOrigin(1920, 1920, 1280) == 2240, "1280 wide in the right-hand 1920");
    Check(CenteredWindowOrigin(48, 1392, 800) == 344, "800 tall in a work area starting at 48");
    Check(CenteredWindowOrigin(-1920, 1920, 1280) == -1600, "a monitor left of the primary one");
}

// Measured on the shipped Steam build: windowed at 2560x1440 comes out as a
// 2576x1460 outer window, taller than a 1440p monitor's 1392px work area. The
// arithmetic centre of that is above the top of the screen, which puts the
// title bar out of reach and leaves the window stuck where it is.
void OversizedWindowTests() {
    std::printf("A window larger than the area is clamped to the area start\n");
    Check(CenteredWindowOrigin(0, 1392, 1460) == 0, "a 1460 tall window in a 1392 work area");
    Check(CenteredWindowOrigin(48, 1392, 1460) == 48, "the same, in a work area starting at 48");
    Check(CenteredWindowOrigin(0, 5120, 2576) == 1272,
          "the same window's width still centres on an ultrawide");
}

// The game puts its own 1936x1119 window at y=137 on a 1392px work area, where
// the arithmetic centre is 136.5. Treating that pixel as a miss would move the
// window for nothing and log a fix that fixed nothing.
void AlreadyCentredTests() {
    std::printf("A window within a pixel or two of the centre counts as centred\n");
    Check(IsAlreadyCentred(136, 136), "exactly centred");
    Check(IsAlreadyCentred(137, 136), "the game's own rounding, one pixel low");
    Check(IsAlreadyCentred(134, 136), "two pixels the other way");
    Check(!IsAlreadyCentred(139, 136), "three pixels out is not centred");
    Check(!IsAlreadyCentred(-43, 0), "the title bar off the top of the screen is not centred");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - game window tests\n");
    std::printf("=======================================================\n");
    FittingWindowTests();
    OffsetAreaTests();
    OversizedWindowTests();
    AlreadyCentredTests();
    return wf_test::Summary("game window");
}
