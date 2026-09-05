# Wreckfest 2 Head Tracking

![Wreckfest 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/wreckfest-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Wreckfest 2 that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **6DOF tracking** - rotation and lean, so you can look into a corner and shift your head to see past the A-pillar.
- **Works with any OpenTrack-compatible source** - webcam, phone app, or anything else that sends the OpenTrack UDP protocol

## Requirements

- [Wreckfest 2](https://store.steampowered.com/app/1203190/) on Steam.
- A head tracking source that can send the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/wreckfest-2-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure your tracker to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, tell it where the game is. Either set the environment variable:

```powershell
$env:WRECKFEST_2_PATH = "D:\Games\Wreckfest 2"
.\install.cmd
```

or pass the folder as an argument:

```powershell
.\install.cmd "D:\Games\Wreckfest 2"
```

### Manual Installation

To place the files by hand, from the extracted installer ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into the game folder next to `Wreckfest2.exe` and rename it to `version.dll`. This is the ASI loader.
2. Copy `plugins\Wreckfest2HeadTracking.asi` into the same folder.

The mod writes `HeadTracking.ini` and `HeadTracking.log` beside `Wreckfest2.exe` on first run.

## Setting Up OpenTrack

In OpenTrack, set **Output** to `UDP over network`, open its options and set the address to `127.0.0.1` and the port to `4242`. Pick an **Input** to match your hardware, then press Start.

Centering is done in your tracker, not in the game. Use OpenTrack's Center bind, SteamVR's reset, or the CENTER button in your phone app.

### VR Headset Setup

If OpenTrack can see your headset as an input, the mod takes its pose like any
other source.

1. Start SteamVR and confirm the headset is tracking.
2. In OpenTrack, set **Input** to the SteamVR tracker.
3. Leave **Output** on `UDP over network`, `127.0.0.1:4242`.

### Webcam Setup

1. In OpenTrack, set **Input** to `neuralnet tracker`, which needs no markers, no clip, and no IR hardware.
2. Open its options and pick your webcam.
3. Leave **Output** on `UDP over network`, `127.0.0.1:4242`.

### Phone App Setup

The mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone app is usable here if it sends that protocol itself, or ships a PC-side companion that does. Check your app against that before assuming it fits.

What decides the wiring is how much filtering the app does before the packet leaves the phone. An app that filters on-device can point straight at your PC's LAN IP on port `4242`. A raw or lightly filtered feed sent direct will jitter, because the mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one. That app should send to OpenTrack instead, so OpenTrack's filters and curves can clean the feed up before it reaches the game.

The test is quick: try sending direct, hold your head still, and if the camera drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works the same way.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker running on this same PC if it sends to your LAN address instead of `127.0.0.1`, because the mod classifies the transport, not the machine.

## Controls

Both columns fire the same action. Use whichever your keyboard has: the nav-cluster key, or the chord if your keyboard has no nav cluster.

| Action | Nav cluster | Chord |
|--------|-------------|-------|
| Toggle head tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode (rotation and position / rotation only / position only) | `Page Up` | `Ctrl+Shift+G` |

Both are remappable in `HeadTracking.ini`.

## Configuration

`HeadTracking.ini` is written next to `Wreckfest2.exe` on first run. Edit it and restart the game to apply. The settings it holds, with the shipped defaults:

```ini
[Network]
UdpPort=4242

[General]
EnableOnStartup=1

[Hotkeys]
; Windows virtual key codes, in hex. Each action has a nav-cluster key and a
; Ctrl+Shift+<key> chord, and both fire it - remap either or both.
; Common codes: End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21, PgDn 0x22,
; F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.
ToggleKey=0x23
CycleModeKey=0x21
ChordToggleKey=0x59
ChordCycleModeKey=0x47

; Head movement is used exactly as your tracker sends it. There is no
; sensitivity, deadzone or axis inversion here on purpose: set those in
; OpenTrack or your phone app once, and every game behaves the same way.

[Rotation]
; Smoothing covers rotation and position alike, and the value used is picked
; per connection from where the tracker sends from. 0.0 none .. 1.0 heavy.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
; How far the camera may lean from where the game put it, in metres.
; 0 to 0.50 on each axis. A value past either end is pulled back to it and
; the log says so; 0 is a real setting and pins that axis.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10
```

## Troubleshooting

**Mod not loading**

- Check for `HeadTracking.log` next to `Wreckfest2.exe`. No log file means the ASI loader never ran: confirm `version.dll` and `Wreckfest2HeadTracking.asi` are both in that folder.
- If the log says the mod is staying dormant on an unrecognised build, the game has been patched since this release. The mod installs no hooks in that state and the game runs vanilla. Check the [Releases](https://github.com/itsloopyo/wreckfest-2-headtracking/releases) page for a build that knows your version.

**No tracking response**

- Confirm your tracker is sending to `127.0.0.1:4242` and that `UdpPort` in `HeadTracking.ini` matches.
- Head tracking follows during a race, from the grid countdown onwards, online and offline alike. It stays off in menus, in the paddock, on the results screen, and while paused.
- Press `End` or `Ctrl+Shift+Y` in case tracking was toggled off.
- If a phone or a second PC is sending over the network, allow the game through Windows Firewall on private networks.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` for a tracker coming in over the network, or `LocalSmoothing` for one on `127.0.0.1`.
- For a phone app with little on-device filtering, send to OpenTrack and let its filters clean the feed up before it reaches the game.
- For a webcam, more light on your face and a steadier frame rate does more than any setting here.

**Wrong axis, or movement that feels too strong or too weak**

- Fix it in your tracker, not here. The mod has no sensitivity, deadzone or inversion settings on purpose: OpenTrack and the phone apps already have them, and setting them in one place means every game behaves the same way.
- If the camera turns or leans the opposite way to your head, invert that axis in your tracker's output mapping.
- If the view sits off-centre, recenter in your tracker: OpenTrack's Center bind, SteamVR's reset, or your phone app's CENTER button.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Prerequisites: Visual Studio 2022 or newer with the C++ desktop workload, CMake, and [pixi](https://pixi.sh).

```powershell
git clone --recursive https://github.com/itsloopyo/wreckfest-2-headtracking.git
cd wreckfest-2-headtracking
pixi run build
pixi run test
pixi run package
```

`pixi run package` writes the installer ZIP to `release/`. No game install is needed to build.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Bugbear Entertainment](http://bugbeargames.com/) and [THQ Nordic](https://thqnordic.com/) for Wreckfest 2.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, which loads the mod.
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol this mod speaks.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, used for function hooking.

Third-party components and their licenses are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bugbear Entertainment or THQ Nordic. Use at your own risk.
