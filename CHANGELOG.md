# Changelog

## [Unreleased]

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Fixed

- The Portal reticle and portal-status brackets now follow the aim point when
  head tracking moves the view.

### Changed

- Settings move to `CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The chords were fixed in code before; now they can be changed or removed like any other key.
- The tracking mode (`PageUp`) and the yaw mode (`PageDown`) are saved to `CameraUnlock.ini` when they change, so the next launch starts in the mode you left. `End` still changes the session only.
- The old `[Position] Enabled` becomes the tracking mode at startup, `RotationEnabled` and `PositionEnabled`. The old single `LimitY` becomes both `PositionLimitY` and `PositionLimitYDown`, which it already set, and each can now be set on its own. `WorldSpaceYaw` moves to `[General]`. `[View] Fov`, `[View] FovViewmodel` and `[Debug] LogToFile` keep their names.
- A `HeadTracking.ini` with `LimitX`, `LimitY`, `LimitZ` or `LimitZBack` above 10 metres, which `CameraUnlock.ini` cannot hold, is not imported. The game runs on the settings it holds, nothing is saved that session, the log says which value stopped the import, and the next start tries again.
- The HeadTracking.ini reader is unchanged since the published development build, and so is how the mod starts from what it read, so apart from the changes listed here every setting you had carries over as it was.
- The default UDP port is now `4242`, was `5626`. It is `UdpPort` under `[Network]` in `CameraUnlock.ini`.
- The vendored Ultimate ASI Loader is no longer reported as tampered. Upstream
  ships `dinput8.dll` Authenticode signed, and zeroing the embedded third-party
  resources breaks the hash that signature covers, so Windows reported the
  vendored copy as `HashMismatch`, "changed by an unauthorized user or process".
  `scripts/strip-loader-payload.ps1` now removes the certificate table and its
  data directory entry and recomputes the optional-header `CheckSum`, so the
  copy we redistribute reads as cleanly unsigned. The loader's code, imports,
  relocations and appended PDB are still byte-identical to upstream.
- `THIRD-PARTY-NOTICES.md` now reproduces the licence text of everything the
  release ZIPs carry. It named MIT and BSD-3-Clause without reproducing either,
  and omitted MinHook, injector, miniz, MemoryModule and d3d8to9 entirely, all
  five of which are compiled into the loader binary the installer ZIP
  redistributes. MPL-2.0 also requires a source offer for MemoryModule, which is
  now recorded.
- The README no longer documents behaviour this repo does not build. It carries
  a status note saying the payload is not written yet, and the config keys it
  lists are the ones the shared parser actually resolves: `[Deadzone]`,
  `WorldScale` and `[Debug] LogToFile` were documented but bind to nothing,
  bare `Yaw`/`Pitch`/`Roll` and `[Position] Enabled` are deliberately not
  aliased, and the hotkeys take key names rather than virtual-key codes.

### Fixed

- The `HEADTRACKING_VERSION_STRING` pattern is anchored to its `#define`. It
  matched the macro name anywhere, and since the read takes the first match
  while the write replaces every match, a comment mentioning the macro was read
  as the version and then rewritten. `.github/workflows/release.yml` carries the
  same pattern, so its tag-vs-file check agreed with the wrong value.
- A resource data entry whose RVA resolves outside `.rsrc` is rejected. The walk
  bounded the 16-byte data entry but not the payload address inside it, so an
  entry pointing at raw-backed bytes in another section had those bytes zeroed,
  and `-VerifyOnly` then reported the image clean.
- The version files are proved writable before any of them is written, so a
  read-only or locked file can no longer leave a half-applied bump.
- `pixi run test` runs in CI. The behaviour locks for the two scripts that can
  silently ship a wrong artifact existed but nothing invoked them on any path.
- `pixi run release` refuses a tag that already exists on the remote, and checks
  the version files before regenerating the CHANGELOG rather than after.
- `scripts/deploy.ps1` runs the same x86 check the packager does, so a build
  configured for the wrong platform fails at deploy instead of installing
  cleanly and doing nothing.

### Removed

- The sensitivity, scale, deadzone, response curve and axis inversion settings. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before: `[Position] WorldScale` shipped at 39.37 Source units per metre, which the mod now applies itself.
