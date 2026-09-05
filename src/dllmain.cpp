// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

// No DLL_PROCESS_DETACH handling. The module pins itself against unloading in
// Initialize and stays dormant when it cannot, so a detach only ever arrives at
// process exit - where the kernel has already killed the other threads without
// unwinding, and joining one from under the loader lock would deadlock rather
// than tidy up. There is nothing left that can be undone safely, so nothing is.
//
// No DisableThreadLibraryCalls either: this DLL links the static CRT, which
// uses the per-thread notifications that call suppresses, and the mod creates
// its handful of threads once at load, so there is nothing to gain by it.
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) wf2_ht::Initialize();
    return TRUE;
}
