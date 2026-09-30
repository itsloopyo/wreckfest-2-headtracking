# Changelog

## [0.2.0] - 2026-09-30

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- `PositionLimitYDown`, how far lowering your head can move the view, separate from `PositionLimitY` above it. `HeadTracking.ini` had one `LimitY` for both, which is imported into each.

### Changed

- Settings move to `CameraUnlock.ini`, next to `Wreckfest2.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A first start with no `HeadTracking.ini` no longer writes one. It creates `CameraUnlock.ini` instead.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] ToggleKey` and `ChordToggleKey`, virtual key codes, are imported together into `ToggleKey`, and `CycleModeKey` and `ChordCycleModeKey` into `CycleTrackingModeKey`, each as the plain key and Ctrl+Shift with the chord key.
- The tracking mode (Page Up / Ctrl+Shift+G) is saved to `CameraUnlock.ini` when you change it, and the game starts in the mode you left it in. Turning tracking on or off (End / Ctrl+Shift+Y) is still not saved; the game starts with head tracking on or off as `EnableOnStartup` says.
- Pressing the tracking mode key twice before the game draws its next frame now moves one mode on, not two, so the mode the game switches to is always the mode that was saved.
- The keys are renamed to the names every head tracking mod on `CameraUnlock.ini` uses: `LocalSmoothing` and `RemoteSmoothing` move from `[Rotation]` to `[Smoothing]`, and the lean limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`. `[Position] Enabled`, which chose the mode tracking started in, is imported as that mode: `Enabled=0` starts in rotation only, as it did.
- A value outside a setting's range is no longer pulled back to the nearest end. The setting keeps its default, and the log names the line. The lean limits take 0 to 10 metres, where `HeadTracking.ini` held them to 0.50, and `UdpPort` takes 1 to 65535, where `HeadTracking.ini` took 1024 to 65535. Every value `HeadTracking.ini` could hold imports as it was read.

## [0.1.0] - 2026-09-05

### Added
- Head tracking now follows the head through the grid countdown, not just from the green light. The gate reads the engine's race phase instead of its green light byte, so the view follows while the lights are still counting down.

### Fixed
- Head tracking now works in a multiplayer race the player is driving in. The gate required the engine's frontend flag to be clear on top of the race state, and that flag stays raised for the whole of an online race a player participates in, so tracking worked when spectating a freshly joined server and never when racing. The race state alone decides the gate now.

### Changed
- Head tracking now follows the head in multiplayer races as well as single player, matching the Wreckfest 1 mod. The camera pose is composed for the frame being drawn and taken back out before the engine interpolates from it, so car control, physics and everything sent to the server read the camera the game computed. The network state is still read and written to the log, so a bug report can say whether a race was online.

## [0.0.0] - 2026-09-04

### Added
- Initial release.
- Added 6DOF head tracking driven by any OpenTrack compatible tracker over UDP on port 4242, moving the camera while the wheel or controller keeps steering.
- Added End / Ctrl+Shift+Y to toggle tracking and Page Up / Ctrl+Shift+G to cycle rotation and position, rotation only, and position only.
- Added a self documenting HeadTracking.ini written on first run, covering port, hotkeys, smoothing and position limits.
- Added build fingerprinting so the mod stays dormant on a Wreckfest 2 build it does not recognise.
- Added window centring on startup when the game runs windowed.
