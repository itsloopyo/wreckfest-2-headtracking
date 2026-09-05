# Changelog

## [Unreleased]

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
