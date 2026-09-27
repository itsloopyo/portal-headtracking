# Portal Head Tracking

An unofficial head tracking mod for Portal that moves the view with your head
while your mouse or controller keeps aiming, driven by an OpenTrack UDP feed,
with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; the portal gun still aims with your mouse or controller
- **6DOF positional tracking** - lean, peek and duck by moving your head
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Portal](https://store.steampowered.com/app/400/Portal/) on Steam, legitimately purchased.
- A tracking source that sends the OpenTrack UDP pose protocol, such as [OpenTrack](https://github.com/opentrack/opentrack/releases).
- Windows. Portal runs as a 32-bit process, so the mod and its loader are both x86; 64-bit Windows runs them fine.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Portal**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP (`PortalHeadTracking-v<version>-installer.zip`) from the [Releases page](https://github.com/itsloopyo/portal-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds Portal, places the loader and the mod in `<game>\bin\`, and reports what it did.
4. Configure OpenTrack, or your tracker app, to send UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your copy of the game, pass the path as the first
argument:

```powershell
.\install.cmd "D:\Games\steamapps\common\Portal"
```

That is the folder containing `hl2.exe`.

### Manual Installation

For placing the files by hand, or when using the Nexus ZIP
(`PortalHeadTracking-v<version>-nexus.zip`), which mirrors the game folder and
carries the mod only, with no loader.

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) and take `dinput8.dll` out of `Ultimate-ASI-Loader.zip`. That asset is the x86 build; the x64 one cannot load into Portal.
2. Rename it to `winmm.dll` and put it in `<game>\bin\`. Source loads its DLLs out of `bin\` with an altered search path, so a proxy DLL next to `hl2.exe` is never loaded and nothing happens. If another mod already put an Ultimate ASI Loader proxy in that folder, leave it alone and skip this step.
3. Put `PortalHeadTracking.asi` in that same `bin\` folder. Extracting the Nexus ZIP over `<game>\` lands it there for you.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on the UDP port set by `UdpPort` under
`[Network]` in `CameraUnlock.ini`. It reads `default` there, which takes the port
from `Defaults.ini`, or `4242` where `Defaults.ini` sets none. In OpenTrack, set **Output** to `UDP over network` and enter
host `127.0.0.1` and that port. Map yaw, pitch and roll, and X, Y and Z as well
if you want positional tracking. Press **Start**. Tracking and the game can
start in either order.

Configure OpenTrack's **Input** per OpenTrack's own documentation; whichever
input you pick, the output settings above are what this mod reads. Recentring is
done in your tracker, using whatever recentre control it offers.

### Phone App Setup

The mod reads OpenTrack UDP pose packets. A phone app is usable here only if it
sends that protocol itself, or ships a PC-side companion that does, so check
yours for an OpenTrack or UDP output option first.

An app that can target an arbitrary address can point straight at this PC's LAN
address (run `ipconfig` to find it) on the port above. If the view drifts or
shakes when you hold your head still, either raise `RemoteSmoothing` or point
the app at OpenTrack's `UDP over network` **input** on some other port, say
`5252`, and let OpenTrack's filters run before its output forwards to
`127.0.0.1:4242`.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody
with a phone already in their pocket.

A phone on WiFi is a remote connection and is smoothed with `RemoteSmoothing`
rather than `LocalSmoothing`. So is a tracker running on this same PC that sends
to the machine's LAN address instead of `127.0.0.1`, because the mod reads the
source address of the packet and not the machine it came from.

## Controls

Each action has a list of keys, and any key in it fires the action. By default
each list holds a nav-cluster key and a chord, so use whichever your keyboard has:

| Action              | Default keys               | Setting                |
|---------------------|----------------------------|------------------------|
| Toggle tracking     | `End`, `Ctrl+Shift+Y`      | `ToggleKey`            |
| Cycle tracking mode | `PageUp`, `Ctrl+Shift+G`   | `CycleTrackingModeKey` |
| Toggle yaw mode     | `PageDown`, `Ctrl+Shift+H` | `YawModeKey`           |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. 6DOF, rotation and position together
2. Rotation only, positional tracking off
3. Position only, rotational tracking off
4. Back to 6DOF

`Page Down` / `Ctrl+Shift+H` switches yaw between horizon-locked (yaw around the
world up axis, the default) and camera-local (yaw composed with the camera's
current pitch and roll).

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment
they change, so the next launch starts in the mode you left it in. `End` turns
tracking on and off for the session only and saves nothing: whether tracking is
on at launch is `EnableOnStartup`.

Every key in the three lists, the chords included, can be changed or removed
under `[Hotkeys]` in `CameraUnlock.ini`, for example `ToggleKey=F8, Ctrl+Shift+Y`.
Hotkeys only fire while the Portal window has focus.

Recentring is not one of these, and nothing polls a recentre key: the tracker
app owns the centre.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
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
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Portal head tracking settings.
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
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
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
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[View]
; Field of view in degrees, as the game's fov_desired: horizontal, at 4:3, and the mod
; widens it for your screen as the game does. 0 leaves the game's own. Otherwise 30 to
; 150, which fov_desired's own 75 to 120 does not bound. A zoom still narrows the view by
; the factor it always did. Applies only while head tracking is on.
Fov=0.0
; Field of view the weapon in your hands is drawn with, in the same degrees. A wider Fov
; leaves the weapon looking oversized: lower this to shrink it. 0 leaves the game's own.
FovViewmodel=0.0

[Debug]
; true: write HeadTracking.log beside hl2.exe, new at every launch, with the launch before
; kept as HeadTracking.prev.log. It records the game build, the tracker connection and
; the view the mod draws. Attach it to a bug report.
LogToFile=true
```
<!-- /cameraunlock:config -->

## Troubleshooting

**Mod not loading.**

- Check that `winmm.dll` and `PortalHeadTracking.asi` are both in `Portal\bin\`, and not next to `hl2.exe`.
- Check the loader is the x86 build. The x64 one cannot load into a 32-bit process, and it fails silently.

**No tracking response.** The mod loads but the view does not move.

- Check the tracker is running with its output set to UDP `127.0.0.1:4242`, and that your firewall is not blocking that port.
- If another app is holding the UDP port, usually another game you left running, close it; the receiver retries the bind on an interval.
- Check tracking is not toggled off: press `End` (or `Ctrl+Shift+Y`), and check `EnableOnStartup` in `CameraUnlock.ini` has not been set to `false`.

**Jittery or unstable tracking.** Raise the smoothing value that applies to your
setup in `CameraUnlock.ini`: `RemoteSmoothing` for a phone or other network
tracker, `LocalSmoothing` for a tracker on this PC. Try 0.3 and work down.

**Leaning or turning moves the view the wrong way.**

- The mod applies the head pose as your tracker sends it, with no inversion of its own. Flip the axis in your tracker app.
- If yaw feels wrong only when looking steeply up or down, toggle between horizon-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`).

## Updating

Download the new release and run `install.cmd` again. Your settings in `CameraUnlock.ini` are kept.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs and its log files, and leaves
`CameraUnlock.ini` in place, so a reinstall keeps your settings. The ASI loader
is only removed if the installer put it there; use `uninstall.cmd /force` to remove it anyway.
Do that only if no other mod needs it.

## Building from Source

Requires Visual Studio with the C++ workload, CMake, and pixi. CMake picks
whichever Visual Studio it finds. Portal is a 32-bit Source Engine game, so the
payload is built for `x86`.

```powershell
git clone --recursive https://github.com/itsloopyo/portal-headtracking
cd portal-headtracking
pixi run test
pixi run build
pixi run package
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

Third-party components bundled in or linked into the release are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) with their own licenses.

## Credits

- Portal and the Source Engine (C) Valve Corporation.
- [OpenTrack](https://github.com/opentrack/opentrack) for the UDP pose format this mod reads. No OpenTrack code is used.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT).
- [CameraUnlock core](https://github.com/itsloopyo/cameraunlock-core) (MIT) for the shared tracking pipeline and installer bodies.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Valve
Corporation. Use at your own risk.
