# PPSSPP Switch Port — Project Context

Fork of **m4xw/ppsspp** (`rebase_2026`), a Switch homebrew port based on **v1.19.3** of hrydgard/ppsspp.

**Active branch: `switch-enhancements`.** All new work goes here.

**Remotes:** `origin` = m4xw/ppsspp (base fork) · `hrydgard` = upstream PPSSPP (cherry-picks, version tags) · `amurgshere` = this fork (push here only, never `origin`/`hrydgard`).

## Switch build (Docker)

Container `ppsspp-build` (`devkitpro/devkita64:latest`), persistent — `docker start ppsspp-build` if stopped. Source mounted at `/app`, build output at `/app/build-switch/`.

Rebuild:
```powershell
docker exec ppsspp-build bash -lc 'stdbuf -oL -eL make -C /app/build-switch -j$(nproc) 2>&1'
```
Generate NRO (make doesn't do this):
```bash
docker exec ppsspp-build bash -c "nacptool --create 'PPSSPP' 'PPSSPP Team' '1.19.3' /app/build-switch/PPSSPP_GL.nacp && elf2nro /app/build-switch/PPSSPPSDL.elf /app/build-switch/PPSSPP_GL.nro --nacp=/app/build-switch/PPSSPP_GL.nacp --icon=/app/icons/icon-512.jpg"
```
Copy `PPSSPP_GL.nro` to the Switch SD card as `switch/PPSSPP_GL/PPSSPP_GL.nro` (~30MB).

Container recreation / CMake reconfigure steps: see memory (`project-docker-setup`) — rarely needed.

## Windows build (local, no CI)

VS Build Tools 2022, MSBuild at the default install path.
```powershell
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
& $msbuild /m /p:TrackFileAccess=false /p:Configuration=Release /p:Platform=x64 Windows\PPSSPP.sln
```
Run from repo root. Output: `PPSSPPWindows64.exe` (~19MB).

Add `/t:<ProjectName>` to build one project (name may differ from folder/vcxproj):
- `/t:UnitTest` → `Windows\x64\Release\UnitTest.exe` (run all: no args; run one: `UnitTest.exe <TestName>`)
- `/t:PPSSPPHeadless` → `Windows\x64\Release\PPSSPPHeadless.exe`

## pspautotests headless suite

`test.py` (repo root) drives the `pspautotests` submodule via the headless exe (auto-detected). Use a real `python.exe`, not the Windows Store alias stub — check with `where.exe python`.

- `python test.py -g` = `tests_good` (must pass) · `-b` = `tests_next` (WIP, not required). Combining `-g -b` only runs `-g`.
- `test.py <name>` silently skips names also in `tests_ignored` — edit that list locally (don't commit) to test something on it.
- If a test fails with "Test init failed", **check the `.prx` actually exists** in the submodule checkout before assuming an emulation bug — the pin was once >2.5yr stale, causing ~20 tests to have no binary at all. Bump via `git ls-tree hrydgard/master pspautotests` to match upstream's pin.

## NRO build/commit workflow

Always build the NRO before committing. Never push without explicit permission. No `Co-Authored-By` trailers.

1. Increment the build counter (tracked in memory) only when ready to commit, not per rebuild iteration.
2. Build NRO as `PPSSPP_GL_NNNN.nro` in `build-switch/`. Overwrite in place for fix iterations (don't advance counter).
3. User reviews on-Switch.
4. Commit (only when user asks) → rename NRO to `PPSSPP_GL_NNNN_hhhhhhhh.nro` using `git rev-parse --short HEAD` **taken after** the commit.
5. Push only when explicitly told. Same pattern for `PPSSPPWindows64_NNNN_hhhhhhhh.exe`.

## Merging Switch code into shared files

m4xw's port often added Switch-only code to shared files without platform guards (`#if PPSSPP_PLATFORM(SWITCH)` / `if(USE_LIBNX)`), breaking other platforms. If a non-Switch build breaks after merging Switch-side work, check for this first. Detailed offender list: memory (`project-m4xw-breakages`).

## Versioning

`git describe` (tags from `hrydgard` remote) → `v1.19.3-N-gHASH`.
