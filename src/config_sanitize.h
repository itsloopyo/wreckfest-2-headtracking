// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

namespace wf2_ht {

// Boundary validation for values read from the user-editable HeadTracking.ini.
// IniReader parses floats with strtod, which accepts "nan" and "inf" and
// overflows a literal like 1e400 to +inf, so a typo or a corrupted file feeds
// those straight into the smoothing math, the quaternion, and from there into
// the camera transform this mod writes back into the engine. Every float that
// crosses that boundary goes through one of these first.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// LocalSmoothing and RemoteSmoothing must each be finite and within [0,1].
// [0,1] is the whole meaningful domain: CalculateSmoothingFactor maps it onto a
// settle speed between 50 (frame interpolation only) and 0.1 (roughly a five
// second settle), and the core clamps that speed to [0.1, 50] itself, so a
// value outside the range no longer drives the per-frame factor negative. It
// just saturates at one end while the INI goes on advertising a setting the mod
// is not honouring, so the clamp stays: it keeps the stored value and the
// behaviour in agreement, and gives the caller something to log.
//
// This is validation, never a floor. Any value inside [0,1] reaches the
// processor untouched, 0.0 included. `fallback` is the shipped default of the
// key being read, 0.0 for LocalSmoothing and 0.15 for RemoteSmoothing, so a
// malformed RemoteSmoothing lands on the remote default instead of silently
// handing a phone-over-WiFi user the local "no smoothing at all".
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// A virtual key code the hotkey poller can actually watch. GetAsyncKeyState
// only defines 0x01..0xFE, so a typo like ToggleKey=0x230 registers a hotkey
// that can never fire and the key silently does nothing.
//
// The modifiers are refused for a second reason: Ctrl and Shift are what the
// chord guard tests, so an action bound to one either never fires (a nav
// binding is suppressed while the chord is held) or fires on every press of
// any chord. Alt sits with them because it is the same class of key and a
// binding on it reads as a modifier the user expects to combine, not press.
inline bool IsBindableVirtualKey(int v) {
    if (v < 0x01 || v > 0xFE) return false;
    if (v >= 0x10 && v <= 0x12) return false;  // Shift, Control, Alt
    if (v >= 0xA0 && v <= 0xA5) return false;  // and their left/right halves
    return true;
}

// Travel limits in metres. PositionProcessor clamps each axis to
// [-limit, +limit], so a negative limit inverts the clamp bounds and a
// non-finite one propagates NaN into the camera translation.
//
// The upper bound is the documented maximum rather than generous headroom,
// because the typo worth catching is a misplaced decimal point - 4.0 for 0.40,
// 3 for 0.30 - and not a mistyped 10000. A metres-wide bound passes both of
// those untouched and unlogged, and puts the render eye several metres outside
// the car; 0.5 refuses them and says which value it used instead. A cockpit
// head has centimetres of travel, so nothing usable is lost at the top.
constexpr float kMaxPositionLimit = 0.5f;

inline float SanitizePositionLimit(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, kMaxPositionLimit);
}

}  // namespace wf2_ht
