# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader (x86), the install-time source of truth.
install.cmd copies it straight out of here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.4`
- Commit: `6b440669144c4a0bef5718ab155df160d231cd42`
- Asset: `Ultimate-ASI-Loader.zip`
- Asset URL: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader.zip
- Upstream dinput8.dll SHA-256: `d5a059aa467a7a7127c8f6169f79fa63ff0f55986ee9eb2fd9a281bebf2aa2e6`
- Vendored dinput8.dll SHA-256: `890f02057c76b673f03a8b696c6295822c22f3370de9bf9ad232d6c6c8078af7` (after the strip below)
- Fetched at: 2026-09-05T15:36:18.1891155+01:00

install.cmd copies `dinput8.dll` to <game>\bin\winmm.dll, the proxy slot Portal
loads ASI plugins through.

## Modified: third-party payload stripped

The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,
so that a user who renames it over one of those libraries still gets the original
exports, plus the ini template one of them reads:

- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary
  middleware licensed per title; we have no right to redistribute it.
- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.
  No licence accompanies it.
- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.

`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,
before the file is committed. The loader code, its imports, relocations and appended
PDB are byte-identical to upstream. Outside `.rsrc`, the same script removes the
upstream Authenticode certificate table and recomputes the optional-header CheckSum:
zeroing a resource invalidates the signature, and Windows reports a file whose
signature no longer verifies as tampered rather than as unsigned. The vendored copy
is unsigned. Nothing in this mod can reach the stripped resources - the two library
payloads are keyed off the loader's own filename, and we deploy it as `winmm.dll`,
while the windower needs a `wndmode.ini` we never ship. MIT permits the modification;
it is recorded here and in THIRD-PARTY-NOTICES.md so this copy is not mistaken for
stock upstream.
