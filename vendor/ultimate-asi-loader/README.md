# ultimate-asi-loader (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `Ultimate-ASI-Loader_x64.zip`
- Tag: `v9.7.4`
- Commit: `6b440669144c4a0bef5718ab155df160d231cd42`
- Upstream URL: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader_x64.zip
- SHA-256: `8272d83b2692662098746f2d0ad0e2d85f3c8358ab1d63f75fbe835c2c8135fd`
- dinput8.dll SHA-256: `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`
- Fetched at: 2026-08-31T23:56:39.9556742+01:00
- Source: github

Do not edit this directory by hand. Run `pixi run update-deps` to refresh, then commit.

install.cmd copies this DLL to the game root as `version.dll`, not under its
upstream name: Wreckfest2.exe statically imports both DINPUT8.dll and
VERSION.dll, and neither is a KnownDLL, so a game-local copy of either wins
the safe DLL search order. VERSION.dll is the one used because it proxies
only the four GetFileVersionInfo entry points, while DINPUT8.dll carries the
gamepad API this game genuinely calls.
