# Portal Head Tracking

An unofficial head tracking mod for Portal that moves the view with your head
while your mouse or controller keeps aiming, driven by an OpenTrack UDP feed,
with no VR headset required.

> **Status: the mod is not built yet.** This repository currently holds the
> release tooling, the installer wrappers and the vendored ASI loader. There is
> no `src/` payload, no `CMakeLists.txt` and no `PortalHeadTracking.asi`, so
> there is nothing to download and `pixi run package` does not yet produce a
> ZIP. `install.cmd` does not work either: it looks the game up by id in
> `cameraunlock-core/data/games.json`, which has no `portal` entry yet, so it
> fails with `Unknown game id: portal` and exits non-zero. Passing the path
> explicitly does not get round that, because the id lookup runs before the
> path is considered. The sections below describe what the mod is meant to do
> and what the shared library it will be built on already accepts. They are a
> specification, not a description of shipped behaviour.

## Features

- **Decoupled look and aim** - head tracking moves the camera; the portal gun still aims with your mouse or controller
- **6DOF positional tracking** - lean, peek and duck by moving your head
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Portal](https://store.steampowered.com/app/400/Portal/) on Steam, legitimately purchased.
- A tracking source that sends the OpenTrack UDP pose protocol, such as [OpenTrack](https://github.com/opentrack/opentrack/releases).
- Windows. Portal runs as a 32-bit process, so the mod and its loader are both x86; 64-bit Windows runs them fine.

## Installation

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

The mod listens for OpenTrack pose data on the UDP port set by `Port`, which
defaults to `4242`. In OpenTrack, set **Output** to `UDP over network` and enter
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

Each action has a nav-cluster key and a `Ctrl`+`Shift` chord, for keyboards
without a nav cluster. Both do the same thing; the nav key is ignored while
`Ctrl`+`Shift` is held, so holding the chord never fires the action twice.

| Action              | Default key | Chord          | Config key  |
|---------------------|-------------|----------------|-------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` | `Toggle`    |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` | `ModeCycle` |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H` | `YawMode`   |

`Page Up` cycles the tracking mode 6DOF -> rotation-only -> position-only and
back. `Page Down` switches yaw between horizon-locked (default) and
camera-local; see `WorldSpaceYaw` below.

Rebinds are virtual-key codes in hex, not key names - `Toggle=0x23` is `End`.
A rebind onto a bare `Y`, `G` or `H` is refused, because it would make the
letter itself a hotkey and typing in the developer console would trigger it.

Recentring is not one of these, and nothing polls a recentre key: the tracker
app owns the centre.

## Configuration

`HeadTracking.ini` is read once at startup, so restart the game after editing
it. Any key you leave out falls back to its default, and a missing file is a
full default set.

Section headers are cosmetic. The parser matches on key names alone and ignores
any key it does not recognise, so a misspelled key is silently inert rather than
an error. The block below is exactly what the mod writes on first run.

```ini
; Portal head tracking - default config

[Network]
Port=4242
EnableOnStartup=1

[Sensitivity]
Yaw=1
Pitch=1
Roll=1
InvertYaw=0
InvertPitch=0
InvertRoll=0

[Smoothing]
; Picked per connection from the tracker's source address, and applied
; to both rotation and position. 0 = no smoothing, 1 = heavy.
; LocalSmoothing: tracker runs on this machine (loopback)
LocalSmoothing=0
; RemoteSmoothing: tracker is a remote device on the network
RemoteSmoothing=0.15

[Deadzone]
Yaw=0
Pitch=0
Roll=0

[Position]
; 6DOF head position, applied to the render view origin only
Enabled=1
; WorldScale = Source units per metre of head movement (1 unit = 1 inch; 39.37 = 1:1)
WorldScale=39.37
SensX=1
SensY=1
SensZ=1
; Flip an axis if leaning moves the view the wrong way. Trackers
; disagree on whether they report in your frame or the camera's
; mirrored view of it. Inversion is applied after the limits below,
; so flipping Z keeps the generous forward allowance on leaning in.
InvertX=0
InvertY=0
InvertZ=0
; Movement envelope in metres before world scaling. LimitY bounds travel
; both up and down. Z is the one asymmetric axis: LimitZ is the forward
; lean and LimitZBack the backward one, because leaning in wants more
; room than pulling back does.
LimitX=0.3
LimitY=0.2
LimitZ=0.4
LimitZBack=0.1

[Hotkeys]
; Virtual-key codes in hex, not key names.
Toggle=0x23
YawMode=0x22
; Page Up: cycle 6DOF -> rotation-only -> position-only
ModeCycle=0x21

[View]
; 1 = horizon-locked yaw (default), 0 = camera-local yaw
WorldSpaceYaw=1
; Field of view, same units as the game's fov_desired cvar (horizontal
; degrees at 4:3; the mod widens it for your real aspect ratio as the
; engine does). Written into the render view rather than the cvar, so it
; is not bound by fov_desired's own range. 0 = leave the game's FOV
; alone. Applies only while tracking is enabled (End).
Fov=0
; The weapon is drawn with its own FOV. Widening Fov leaves the gun
; looking oversized against the wider world: LOWER this to shrink it.
; 0 = leave the game's viewmodel FOV alone.
FovViewmodel=0

[Debug]
; Writes HeadTracking.log next to hl2.exe, fresh every launch (the
; previous session is kept as HeadTracking.prev.log, and nothing else). It
; records the build profile, the tracker connection and the pose being
; applied. That is the file to attach to a bug report - leave it on.
LogToFile=1
```

## Troubleshooting

**Mod not loading.**

- Check that `winmm.dll` and `PortalHeadTracking.asi` are both in `Portal\bin\`, and not next to `hl2.exe`.
- Check the loader is the x86 build. The x64 one cannot load into a 32-bit process, and it fails silently.

**No tracking response.** The mod loads but the view does not move.

- Check the tracker is running with its output set to UDP `127.0.0.1:4242`, and that your firewall is not blocking that port.
- If another app is holding the UDP port, usually another game you left running, close it; the receiver retries the bind on an interval.
- Check tracking is not toggled off: press `End`.

**Jittery or unstable tracking.** Raise the smoothing value that applies to your
setup: `RemoteSmoothing` for a phone or other network tracker, `LocalSmoothing`
for a tracker on this PC. Try 0.3 and work down.

**Wrong rotation or lean axis.**

- Leaning moves the view the wrong way: flip `InvertX`, `InvertY` or `InvertZ` under `[Position]`.
- Head rotation is inverted: flip `InvertYaw`, `InvertPitch` or `InvertRoll` under `[Sensitivity]`.
- Yaw feels wrong when looking steeply up or down: toggle between horizon-locked and camera-local yaw with `Page Down`.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The ASI loader is only removed
if the installer put it there; use `uninstall.cmd /force` to remove it anyway.
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

`pixi run test` passes today. `pixi run build` and `pixi run package` do not:
there is no `CMakeLists.txt` and no payload source yet, so CMake has nothing to
configure. See the status note at the top.

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
