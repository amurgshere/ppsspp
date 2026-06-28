# PPSSPP Switch Port — Project Context

## What this repo is

A fork of **m4xw/ppsspp** (`rebase_2026` branch), which is a Switch homebrew port of PPSSPP based on **v1.19.3** of hrydgard/ppsspp. The upstream Switch port lives at https://github.com/m4xw/ppsspp.


## Build environment

Docker container named `ppsspp-build` using `devkitpro/devkita64:latest`. It is persistent — start it with `docker start ppsspp-build` if stopped.

Source is mounted at `/app` inside the container. Build output is at `/app/build-switch/`.

**To rebuild (container already running):**
```powershell
docker exec ppsspp-build bash -lc 'stdbuf -oL -eL make -C /app/build-switch -j$(nproc) 2>&1'
```
Stream this to a file and watch with `Get-Content` — see global CLAUDE.md for the pattern.

**After a successful build, generate the NRO manually** (make does not do this):
```bash
docker exec ppsspp-build bash -c "nacptool --create 'PPSSPP' 'PPSSPP Team' '1.19.3' /app/build-switch/PPSSPP_GL.nacp && elf2nro /app/build-switch/PPSSPPSDL.elf /app/build-switch/PPSSPP_GL.nro --nacp=/app/build-switch/PPSSPP_GL.nacp --icon=/app/icons/icon-512.jpg"
```

Then copy `/app/build-switch/PPSSPP_GL.nro` to the Switch SD card as `switch/PPSSPP_GL/PPSSPP_GL.nro`. The NRO is ~30MB.

### Recreating the container from scratch

```bash
docker run -d --name ppsspp-build \
  -v "H:/Programming/ppsspp:/app" \
  devkitpro/devkita64:latest \
  sleep infinity

docker exec ppsspp-build bash -c "apt-get update && apt-get install -y \
  build-essential cmake git python3 pkg-config libarchive-tools gettext"

docker exec ppsspp-build bash -c "dkp-pacman -S --noconfirm \
  switch-sdl2 switch-ffmpeg switch-dav1d switch-miniupnpc \
  switch-freetype switch-libpng switch-zlib switch-mesa"
```

### Configuring CMake (clean build dir required)

```bash
docker exec ppsspp-build bash -lc "
  git -C /app submodule update --init --recursive && \
  rm -rf /app/build-switch && mkdir /app/build-switch && cd /app/build-switch && \
  cmake /app \
    -DUSE_LIBNX=ON \
    -DUSING_X11_VULKAN=OFF \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS_RELEASE='-Oz -flto -DNDEBUG' \
    -DCMAKE_CXX_FLAGS_RELEASE='-Oz -flto -DNDEBUG' \
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE='-Wl,--strip-all' \
    2>&1 | tail -10"
```

**Critical cmake gotchas:**
- Must use `bash -lc` (login shell) — `DEVKITPRO` only loads in login shells
- `USING_X11_VULKAN=OFF` required — container has no X11 headers
- `CMAKE_TOOLCHAIN_FILE` required — sets up devkitA64 cross-compiler, libnx headers, `-specs=switch.specs`
- Never pass `-DCMAKE_EXE_LINKER_FLAGS` — it overrides the toolchain's library paths and causes `-lnx not found`. Use `_RELEASE` suffixed variants only.
- Always wipe build-switch/ fully before reconfiguring — partial state breaks SDL2/libnx detection



## Versioning

Version string comes from `git describe` — tags fetched from `hrydgard` remote, shows `v1.19.3-N-gHASH`.
