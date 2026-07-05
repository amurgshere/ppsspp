# Docker Switch build container — full setup

Day-to-day rebuild command lives in `CLAUDE.md`. This file is only needed if the `ppsspp-build` container is lost/recreated, or CMake needs a full reconfigure.

## Recreate container from scratch

```bash
docker run -d --name ppsspp-build \
  -v "<path-to-local-clone>:/app" \
  devkitpro/devkita64:latest \
  sleep infinity

docker exec ppsspp-build bash -c "apt-get update && apt-get install -y \
  build-essential cmake git python3 pkg-config libarchive-tools gettext"

docker exec ppsspp-build bash -c "dkp-pacman -S --noconfirm \
  switch-sdl2 switch-ffmpeg switch-dav1d switch-miniupnpc \
  switch-freetype switch-libpng switch-zlib switch-mesa"
```

## Configure CMake (clean build dir required)

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
    -DARMIPS_USE_STD_FILESYSTEM=ON \
    2>&1 | tail -10"
```

## Gotchas

- Must use `bash -lc` (login shell) — `DEVKITPRO` env var only loads in login shells
- `USING_X11_VULKAN=OFF` required — container has no X11 headers
- Never pass `-DCMAKE_EXE_LINKER_FLAGS` (non-`_RELEASE` variant) — overrides toolchain library paths, causes `-lnx not found`
- Always wipe `build-switch/` fully before reconfiguring — partial state breaks SDL2/libnx detection
- `-DARMIPS_USE_STD_FILESYSTEM=ON` required — armips' bundled `ext/filesystem` (ghc) isn't populated/compatible with libnx; GCC 15 on devkitA64 supports `std::filesystem` natively
