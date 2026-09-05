// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The one line a user is asked to read back out of the log to find out which
// keys their config landed on. Registering the bindings needs a keyboard poller
// thread; building the sentence that describes them does not, and that is the
// half that has to stay right - a drifted separator or a lost binding is how
// the line stops naming a key the user has.

#include "hotkeys.h"

#include "hotkey_names.h"
#include "test_support.h"

#include <cstdio>
#include <string>

using wf2_ht::DescribeHotkeys;
using wf2_ht::HotkeyBinding;
using wf2_ht::HotkeyName;
using wf_test::Check;

namespace {

void NoOp() {}

// Built from HotkeyName rather than from literal key names: what a key is
// called is the keyboard layout's business, and this test runs on whatever
// layout the machine has.
std::string Expected(int nav_key, int chord_key, const char* action) {
    return HotkeyName(nav_key) + "/Ctrl+Shift+" + HotkeyName(chord_key) + " " + action;
}

void ShippedBindingsTests() {
    std::printf("The shipped pair of actions describes as one sentence\n");
    const HotkeyBinding bindings[] = {
        { "toggle tracking",     0x23, 0x59, NoOp },
        { "cycle tracking mode", 0x21, 0x47, NoOp },
    };
    const std::string described = DescribeHotkeys(bindings, 2);
    std::printf("  described: %s\n", described.c_str());

    Check(described == Expected(0x23, 0x59, "toggle tracking") + ", "
                     + Expected(0x21, 0x47, "cycle tracking mode") + ".",
          "both actions, comma separated, one full stop at the end");
    Check(described.find(", ,") == std::string::npos, "no empty entry between the two");
}

void SingleBindingTests() {
    std::printf("A single action carries the full stop and no separator\n");
    const HotkeyBinding bindings[] = { { "toggle tracking", 0x23, 0x59, NoOp } };
    const std::string described = DescribeHotkeys(bindings, 1);

    Check(described == Expected(0x23, 0x59, "toggle tracking") + ".",
          "one entry, terminated");
    Check(described.find(',') == std::string::npos, "no trailing comma");
}

// The mod always passes a fixed pair, but the count is a runtime argument and
// the terminator used to be written by overwriting the last character - which
// eats the caller's own text when there is nothing to describe.
void NoBindingsTests() {
    std::printf("No bindings describes as nothing at all\n");
    Check(DescribeHotkeys(nullptr, 0).empty(), "empty, not a lone full stop");
}

// A key the layout has no name for still has to appear as something the user
// can match against what they typed into the INI.
void UnnamedKeyTests() {
    std::printf("An unnamed key falls back to its code\n");
    const HotkeyBinding bindings[] = { { "toggle tracking", 0x07, 0x59, NoOp } };
    const std::string described = DescribeHotkeys(bindings, 1);
    std::printf("  described: %s\n", described.c_str());

    Check(described.find(HotkeyName(0x07)) == 0, "the nav key's name, whatever it is, leads");
    Check(described.find("/Ctrl+Shift+") != std::string::npos, "the chord half is still there");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - hotkey description tests\n");
    std::printf("=======================================================\n");
    ShippedBindingsTests();
    SingleBindingTests();
    NoBindingsTests();
    UnnamedKeyTests();
    return wf_test::Summary("hotkeys");
}
