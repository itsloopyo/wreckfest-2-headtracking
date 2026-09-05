// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The one log line that tells a user which keys their config ended up on. What
// this locks is the contract that holds on every keyboard layout: a name is
// always produced. What each key is CALLED is the layout's business, so nothing
// here asserts a particular string - that would pass on one machine and fail on
// the next.

#include "hotkey_names.h"

#include "test_support.h"

#include <cstdio>
#include <string>

using namespace wf2_ht;
using wf_test::Check;

namespace {

// Every key the shipped INI binds by default, plus the ends of the range the
// config layer will accept.
const int kKeysToName[] = {
    0x23,  // End, the default toggle
    0x21,  // Page Up, the default mode cycle
    0x59,  // Y, the toggle chord
    0x47,  // G, the mode cycle chord
    0x01, 0x70, 0xFE,
};

void EveryKeyGetsAName() {
    std::printf("Every bindable key names itself\n");

    for (int vk : kKeysToName) {
        const std::string name = HotkeyName(vk);
        Check(!name.empty(), "the key produces a non-empty name");
    }
}

// The extended-key bit is the whole reason hotkey_names.cpp exists: the nav
// cluster and the numpad share their scan codes, so MapVirtualKey plus
// GetKeyNameText without it names End "Num 1" and Page Up "Num 9". A test that
// only asks for a non-empty name passes just as happily on the wrong one, which
// is what let the two shipped defaults report themselves as numpad keys.
//
// Asserting the strings themselves would fail on the next keyboard layout.
// Asserting that the two keys do not share a name holds on every layout, and is
// exactly what the extended bit buys: without it, both sides of each pair
// produce the same string.
void TheNavClusterDoesNotNameItselfAsTheNumpad() {
    std::printf("The nav cluster keeps its own names\n");

    Check(HotkeyName(0x23) != HotkeyName(0x61), "End does not name itself as numpad 1");
    Check(HotkeyName(0x21) != HotkeyName(0x69), "Page Up does not name itself as numpad 9");
    Check(HotkeyName(0x2E) != HotkeyName(0x6E), "Delete does not name itself as numpad .");

    // And the layout was actually asked: a name of "0x23" would mean the naming
    // path fell through to the hex fallback, which no shipped default should.
    Check(HotkeyName(0x23) != "0x23", "End reaches the layout, not the hex fallback");
    Check(HotkeyName(0x21) != "0x21", "Page Up reaches the layout, not the hex fallback");
}

void AnUnnameableKeyFallsBackToItsCode() {
    // 0x07 is undefined in the virtual key table, so no layout has a name for
    // it. The code is what the user typed into the INI, so it is what gets
    // reported back.
    std::printf("A key with no name reports its code\n");

    const std::string name = HotkeyName(0x07);
    Check(!name.empty(), "an unnameable key still produces something");
    Check(name.rfind("0x", 0) == 0, "and that something is the hex code");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - hotkey name tests\n");
    std::printf("=======================================================\n");
    EveryKeyGetsAName();
    TheNavClusterDoesNotNameItselfAsTheNumpad();
    AnUnnameableKeyFallsBackToItsCode();
    return wf_test::Summary("hotkey names");
}
