#!/bin/bash
# Rebuilds devkitPro's switch-sdl2 package with PPSSPP's audio-thread hang fix and installs
# it over the stock portlibs copy. Run inside the devkitA64 container after installing switch-sdl2.
set -euo pipefail

SDL_VERSION=2.28.5
DKP_PACKAGES_COMMIT=de1b5b3d5a866709db32a31900e652fba2026f14
SDL_TARBALL_SHA256=332cb37d0be20cb9541739c61f79bae5a477427d79ae85e352089afdaf6666e4
DKP_PATCH_SHA256=eb2da665a014dc540ad3dfeb62b3f7c5e15ae090ebdd631fe8b78c1f272c7aa0
PATCH_MARKER=PPSSPP_SWITCHAUDIO_NOHANG_V1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

source /opt/devkitpro/switchvars.sh

download_sources() {
	curl -fsSL -o "$WORK_DIR/SDL2.tar.gz" "https://libsdl.org/release/SDL2-${SDL_VERSION}.tar.gz"
	curl -fsSL -o "$WORK_DIR/dkp.patch" \
		"https://raw.githubusercontent.com/devkitPro/pacman-packages/${DKP_PACKAGES_COMMIT}/switch/SDL2/SDL2-${SDL_VERSION}.patch"
	echo "${SDL_TARBALL_SHA256}  $WORK_DIR/SDL2.tar.gz" | sha256sum -c -
	echo "${DKP_PATCH_SHA256}  $WORK_DIR/dkp.patch" | sha256sum -c -
}

prepare_source() {
	tar -xzf "$WORK_DIR/SDL2.tar.gz" -C "$WORK_DIR"
	cd "$WORK_DIR/SDL2-${SDL_VERSION}"
	patch -p1 -i "$WORK_DIR/dkp.patch"
	patch -p1 -i "$SCRIPT_DIR/switchaudio-no-hang.patch"
}

# Same configure flags as devkitPro's switch/SDL2/PKGBUILD at the pinned commit.
build_and_install() {
	./configure --prefix="${PORTLIBS_PREFIX}" --host=aarch64-none-elf \
		--disable-shared --enable-static \
		--enable-audio --enable-joystick --disable-power --disable-filesystem \
		--enable-cpuinfo --enable-pthreads --enable-timers --enable-video
	make -j"$(nproc)"
	make install
	rm -rf "${PORTLIBS_PREFIX}/share"
}

verify_installed() {
	if ! grep -q "$PATCH_MARKER" "${PORTLIBS_PREFIX}/lib/libSDL2.a"; then
		echo "error: installed libSDL2.a is missing ${PATCH_MARKER}" >&2
		exit 1
	fi
	echo "Patched SDL2 ${SDL_VERSION} installed to ${PORTLIBS_PREFIX}"
}

download_sources
prepare_source
build_and_install
verify_installed
