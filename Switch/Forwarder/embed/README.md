# Vendored hbloader binary — provenance

`hbl_main` and `hbl_main.npdm` are the ExeFS `main`/`main.npdm` pair used as the
boot "engine" of an installed PPSSPP forwarder title (see `../ForwarderInstaller.h`).
At boot, this binary reads `/nextArgv` and `/nextNroPath` from the forwarder
title's own RomFS (written by `RomfsBuilder`) and chain-loads PPSSPP's NRO from
the SD card with the configured game path as an argument.

## Source

Both files are a compiled build of [switchbrew/nx-hbloader](https://github.com/switchbrew/nx-hbloader)
(ISC License — see `NOTICE-ISC.txt`), vendored via
[TooTallNate/switch-tools](https://github.com/TooTallNate/switch-tools)'
`apps/nsp-forwarder/public/template/exefs/` directory, which periodically
recompiles nx-hbloader (via the `nton` build pipeline) and checks in the
resulting binaries as a template for its own NSP forwarder generator.

Pulled from:
- https://github.com/TooTallNate/switch-tools/blob/main/apps/nsp-forwarder/public/template/exefs/main
- https://github.com/TooTallNate/switch-tools/blob/main/apps/nsp-forwarder/public/template/exefs/main.npdm

Commit history in that repo (`33289c7f`, `5ab94141`) documents this exact binary
as a build of nx-hbloader against libnx 4.12.0-1, including the upstream
HookedBehemoth `selfExit()`/SelfController exit-path fix.

## Checksums (as vendored into this repo)

```
sha256  e135cad353aa4d36d3095a6769e44e5a6f4f82353677b21f8930d1286c741d27  hbl_main
sha256  62091d193ac96b2f80956f80ed731e9ce57b8ec1f4d207fa9d9522db43feef68  hbl_main.npdm
```

## Why not build from source ourselves

This exact binary has already been used in practice (a PPSSPP forwarder built
with it via https://nsp-forwarder.n8.io/, which serves this same template) and
confirmed working on real Switch hardware. Using it directly avoids adding a
new source-build step to the PPSSPP build pipeline for a component we don't
modify beyond patching its NPDM title-ID field at install time (see
`../NcaBuilder.cpp`).

## Updating

If a newer nx-hbloader build is ever needed (e.g. new firmware compatibility),
re-pull both files from the same `switch-tools` template path, update the
checksums above, and note the new upstream commit/tag here.
