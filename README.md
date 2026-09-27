# Wreckfest 2 Head Tracking

![Wreckfest 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/wreckfest-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Wreckfest 2 that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **6DOF tracking** - rotation and lean, so you can look into a corner and shift your head to see past the A-pillar.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Wreckfest 2](https://store.steampowered.com/app/1203190/) on Steam.
- A head tracking source that can send the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Wreckfest 2**, and click
**Play with head tracking**.

### Standalone Installer

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

On first run the mod creates `CameraUnlock.ini`, its settings file, and writes `HeadTracking.log`, both beside `Wreckfest2.exe`.

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

Each action's keys are one list in `[Hotkeys]` in `CameraUnlock.ini`, `ToggleKey` and `CycleTrackingModeKey`, the chord included, so any of them can be changed or removed.

The tracking mode you pick with `Page Up` / `Ctrl+Shift+G` is saved to `CameraUnlock.ini`, and the game starts in it next time. `End` / `Ctrl+Shift+Y` changes the current session only; whether tracking is on when the game starts is `EnableOnStartup`.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`

With every setting at its default, the file reads:

```ini
; Wreckfest 2 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
```
<!-- /cameraunlock:config -->

Changes take effect the next time the game starts.

Hotkeys are written as key names, such as `End`, `PageUp`, `F9` or `Ctrl+Shift+Y`, separated by commas. A key with no name can be written as its Windows virtual key code, `0x` and two hex digits, such as `0xBA`. A value the mod cannot read leaves that setting at its default and is named in `HeadTracking.log`.

Field of view is a game setting, not a mod setting. Wreckfest 2 has its own `CHASE FOV OFFSET` and `1ST PERSON FOV OFFSET` sliders under Settings > Gameplay, each running -10 to +10 degrees of vertical field of view. The mod rotates and moves the camera and never writes its field of view, so whatever those sliders are set to is what you race with.

## Troubleshooting

**Mod not loading**

- Check for `HeadTracking.log` next to `Wreckfest2.exe`. No log file means the ASI loader never ran: confirm `version.dll` and `Wreckfest2HeadTracking.asi` are both in that folder.
- If the log says the mod is staying dormant on an unrecognised build, the game has been patched since this release. The mod installs no hooks in that state and the game runs vanilla. Check the [Releases](https://github.com/itsloopyo/wreckfest-2-headtracking/releases) page for a build that knows your version.

**No tracking response**

- Confirm your tracker is sending to `127.0.0.1:4242` and that the mod's `UdpPort` matches: the value in `CameraUnlock.ini`, or the one in `Defaults.ini` where `CameraUnlock.ini` says `default`. The log names the port it listens on.
- Head tracking follows during a race, from the grid countdown onwards, online and offline alike. It stays off in menus, in the paddock, on the results screen, and while paused.
- Press `End` or `Ctrl+Shift+Y` in case tracking was toggled off.
- If a phone or a second PC is sending over the network, allow the game through Windows Firewall on private networks.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` for a tracker coming in over the network, or `LocalSmoothing` for one on `127.0.0.1`, both in `[Smoothing]` in `CameraUnlock.ini`.
- For a phone app with little on-device filtering, send to OpenTrack and let its filters clean the feed up before it reaches the game.
- For a webcam, more light on your face and a steadier frame rate does more than any setting here.

**Wrong axis, or movement that feels too strong or too weak**

- Fix it in your tracker, not here. The mod has no sensitivity, deadzone or inversion settings on purpose: OpenTrack and the phone apps already have them, and setting them in one place means every game behaves the same way.
- If the camera turns or leans the opposite way to your head, invert that axis in your tracker's output mapping.
- If the view sits off-centre, recenter in your tracker: OpenTrack's Center bind, SteamVR's reset, or your phone app's CENTER button.

## Updating

Download the new release and run `install.cmd` again. Your `CameraUnlock.ini` is kept. Updating from v0.1.0, the first start reads your settings from `HeadTracking.ini` into a new `CameraUnlock.ini`; see [Configuration](#configuration).

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway. `CameraUnlock.ini` and `HeadTracking.ini` are left in place either way, so your settings are still there if you install again.

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

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
