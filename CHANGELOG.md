# Changelog

## [Unreleased]

### Fixed

- The Portal reticle and portal-status brackets now follow the aim point when
  head tracking moves the view.

### Changed

- The default UDP port is now `4242`, was `5626`. It is defined once as
  `kDefaultPort` in `src/config.h`, which feeds the config struct, the
  `HeadTracking.ini` written on first run and the fallback for a missing or
  malformed `Port` key. An existing `HeadTracking.ini` is not rewritten, so an
  installed copy keeps whatever `Port` it already has.
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
