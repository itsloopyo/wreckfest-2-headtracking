// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkey_names.h"

#include <windows.h>

#include <cstdio>

namespace wf2_ht {

namespace {

// GetKeyNameText wants the scan code in bits 16-23 and the extended-key flag in
// bit 24. The nav cluster, the arrows and a few others are extended keys, and
// without that bit they name their numpad twins - a toggle left on End would
// report itself in the log as "Num 1", which is the one thing this line exists
// to get right now that the key is the user's to choose.
bool IsExtendedKey(int virtual_key) {
    switch (virtual_key) {
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_INSERT: case VK_DELETE:
        case VK_DIVIDE: case VK_NUMLOCK: case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

}  // namespace

std::string HotkeyName(int virtual_key) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC);
    if (scan != 0) {
        LONG lparam = static_cast<LONG>(scan) << 16;
        if (IsExtendedKey(virtual_key)) lparam |= 1L << 24;
        char name[64]{};
        if (GetKeyNameTextA(lparam, name, sizeof(name)) > 0) return name;
    }

    // A key this layout has no name for still has to be identifiable, and the
    // code is what the user typed into the INI.
    char code[8]{};
    std::snprintf(code, sizeof(code), "0x%02X", virtual_key);
    return code;
}

}  // namespace wf2_ht
