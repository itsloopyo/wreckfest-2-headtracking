// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The path resolution every file the mod touches hangs off. The mod owns no
// copy of this any more - it calls cameraunlock::os - so what is locked here is
// the contract the bootstrap depends on, not an implementation of it.
//
// The failure that matters is not a wrong directory but a plausible-looking
// empty one: a separator-less module path must NOT resolve to "", because the
// INI path built from it would then be "\HeadTracking.ini" and the mod would
// write the user's config to the root of whatever drive the process is on.
//
// No game needed - only the running test EXE's own module path.

#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <string>

#include "cameraunlock/os/module_paths.h"

using cameraunlock::os::DirectoryOf;
using cameraunlock::os::HostExeDirectory;
using cameraunlock::os::HostExeDirectoryNarrow;
using cameraunlock::os::NarrowToAnsi;
using wf_test::Check;

namespace {

void DirectoryOfTests() {
    std::printf("DirectoryOf splits a module path\n");

    std::wstring dir;
    Check(DirectoryOf(L"C:\\Games\\Wreckfest\\Wreckfest2.exe", dir)
              && dir == L"C:\\Games\\Wreckfest",
          "a normal install path yields the containing directory");

    Check(DirectoryOf(L"C:\\Wreckfest2.exe", dir) && dir == L"C:",
          "an EXE at a drive root yields the drive");

    Check(DirectoryOf(L"\\\\server\\share\\game\\Wreckfest2.exe", dir)
              && dir == L"\\\\server\\share\\game",
          "a UNC path yields the containing directory");

    // The whole reason this is a separate function: the outputs below must stay
    // untouched, not become "".
    std::wstring untouched = L"sentinel";
    Check(!DirectoryOf(L"Wreckfest2.exe", untouched) && untouched == L"sentinel",
          "a separator-less path fails instead of yielding an empty directory");
    Check(!DirectoryOf(L"", untouched) && untouched == L"sentinel",
          "an empty path fails instead of yielding an empty directory");
}

void NarrowToAnsiTests() {
    std::printf("NarrowToAnsi converts for the ANSI-only INI layer\n");

    std::string narrow;
    Check(NarrowToAnsi(L"C:\\Games\\Wreckfest", narrow)
              && narrow == "C:\\Games\\Wreckfest",
          "an ASCII directory converts unchanged");

    Check(NarrowToAnsi(L"C:\\Games\\a b", narrow) && narrow == "C:\\Games\\a b",
          "spaces survive the conversion");

    // An empty wide string cannot be told apart from a conversion failure by
    // WideCharToMultiByte's return, so it reports failure. Reachable only from a
    // module path like "\game.exe", where a failure correctly leaves the mod
    // dormant rather than writing to a drive root.
    std::string untouched = "sentinel";
    Check(!NarrowToAnsi(L"", untouched) && untouched == "sentinel",
          "an empty directory fails instead of yielding an empty string");
}

void HostExeDirectoryTests() {
    std::printf("HostExeDirectory resolves the running module in both encodings\n");

    const std::wstring wide = HostExeDirectory();
    if (!Check(!wide.empty(), "resolves for the running test EXE")) return;

    const std::string narrow = HostExeDirectoryNarrow();
    if (!Check(!narrow.empty(), "and has an ANSI form for the INI layer")) return;

    Check(wide.back() != L'\\', "no trailing separator, so exe_dir + \"\\\" + name is well formed");

    std::string expected;
    Check(NarrowToAnsi(wide, expected) && expected == narrow,
          "the narrow form is the narrowing of the wide form");

    // The resolved directory must actually be this EXE's, not merely a
    // plausible string: appending the file name has to reconstruct the module
    // path Windows reports.
    wchar_t module_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    Check(length > 0 && length < MAX_PATH, "the test EXE's own module path is readable");
    if (length == 0 || length >= MAX_PATH) return;

    const std::wstring full(module_path, length);
    Check(full.compare(0, wide.size(), wide) == 0 && full[wide.size()] == L'\\',
          "the directory is a path prefix of the running module");
    Check(GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES,
          "the directory exists on disk");
}

}  // namespace

int main() {
    std::printf("Wreckfest 2 head tracking - exe path tests\n");
    std::printf("===============================================\n");
    DirectoryOfTests();
    NarrowToAnsiTests();
    HostExeDirectoryTests();
    return wf_test::Summary("exe paths");
}
