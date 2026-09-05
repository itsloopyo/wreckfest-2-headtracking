// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_window.h"

#include <windows.h>

#include "logging.h"

#include "cameraunlock/os/game_window.h"

namespace wf2_ht {

namespace {

// The window turned up between 1.9s and 7.2s after the mod's own boot across
// launches on the machine this was measured on, through Steam off an SSD. The
// rest of the budget is for a cold start off a slower disk; losing the race
// costs the whole feature for that launch, because there is no second look.
constexpr int kPollAttempts = 300;
constexpr DWORD kPollIntervalMs = 100;

// Only a windowed window carries a title bar. Exclusive fullscreen and
// borderless both drop it, and this is the first of the two things that keep
// the mod off them - a size comparison against the WORK AREA cannot tell them
// apart from the windowed case, because this game's windowed frame is larger
// than the work area at the resolutions it is played at.
bool HasTitleBar(HWND window) {
    return (GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) != 0;
}

// The second, and the one that does not depend on a style bit an engine is free
// to leave set: a window whose rect is exactly its monitor's rect is already
// covering the screen. Against the MONITOR rect, not the work area, so the
// windowed case above - taller than the work area, smaller than the monitor -
// still centres.
//
// The caption test alone has only ever been exercised on the windowed case;
// this build's borderless path has never been run. If a borderless window here
// did keep WS_CAPTION, the caption test would pass it through to the centring,
// where the clamp would drop it by the height of the taskbar on every launch
// with nothing the user could do about it. This closes that without resting on
// a style bit.
bool CoversItsMonitor(HWND window) {
    RECT rect{};
    if (!GetWindowRect(window, &rect)) return false;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        return false;
    }
    return rect.left == monitor.rcMonitor.left && rect.top == monitor.rcMonitor.top
        && rect.right == monitor.rcMonitor.right && rect.bottom == monitor.rcMonitor.bottom;
}

// The work area rather than the whole monitor, so a centred window keeps its
// title bar clear of a top-docked taskbar.
void CenterOnWorkArea(HWND window) {
    RECT rect{};
    if (!GetWindowRect(window, &rect)) {
        Log::Line("[window] WARNING: GetWindowRect failed: %lu", GetLastError());
        return;
    }

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        Log::Line("[window] WARNING: GetMonitorInfoW failed: %lu", GetLastError());
        return;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int work_width = monitor.rcWork.right - monitor.rcWork.left;
    const int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
    const int x = CenteredWindowOrigin(monitor.rcWork.left, work_width, width);
    const int y = CenteredWindowOrigin(monitor.rcWork.top, work_height, height);

    if (IsAlreadyCentred(rect.left, x) && IsAlreadyCentred(rect.top, y)) {
        Log::Line("[window] the %dx%d game window is already centred at (%d, %d) on the "
                  "%dx%d work area", width, height,
                  static_cast<int>(rect.left), static_cast<int>(rect.top),
                  work_width, work_height);
        return;
    }

    if (!SetWindowPos(window, nullptr, x, y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("[window] WARNING: SetWindowPos failed: %lu", GetLastError());
        return;
    }

    Log::Line("[window] centred the %dx%d game window at (%d, %d), from (%d, %d), on the "
              "%dx%d work area", width, height, x, y,
              static_cast<int>(rect.left), static_cast<int>(rect.top),
              work_width, work_height);
}

}  // namespace

int CenteredWindowOrigin(int area_start, int area_extent, int window_extent) {
    const int centred = area_start + (area_extent - window_extent) / 2;
    return centred < area_start ? area_start : centred;
}

bool IsAlreadyCentred(int origin, int centred_origin) {
    constexpr int kTolerance = 2;
    const int off = origin - centred_origin;
    return off >= -kTolerance && off <= kTolerance;
}

void CenterGameWindowWhenItExists() {
    // Core's finder rather than a local copy: it is the one that already knows
    // to skip the splash, tooltip and message-only windows a game puts up
    // alongside its render window.
    HWND window = cameraunlock::os::FindGameWindow();
    for (int attempt = 0; attempt < kPollAttempts && window == nullptr; ++attempt) {
        Sleep(kPollIntervalMs);
        window = cameraunlock::os::FindGameWindow();
    }

    if (window == nullptr) {
        Log::Line("[window] no game window appeared within %ds - leaving window placement "
                  "alone", (kPollAttempts * static_cast<int>(kPollIntervalMs)) / 1000);
        return;
    }
    if (!HasTitleBar(window)) {
        Log::Line("[window] the game window has no title bar, so it is fullscreen or "
                  "borderless - leaving it in place");
        return;
    }
    if (CoversItsMonitor(window)) {
        Log::Line("[window] the game window already covers its monitor, so it is "
                  "borderless - leaving it in place");
        return;
    }
    CenterOnWorkArea(window);
}

}  // namespace wf2_ht
