function(require_patched_switch_sdl2)
	set(library "${SDL2_LIBDIR}/libSDL2.a")
	set(marker "PPSSPP_SWITCHAUDIO_NOHANG_V1")

	if(NOT EXISTS "${library}")
		message(FATAL_ERROR "Switch SDL2 library not found at ${library}")
	endif()

	# Re-run this check whenever the library is replaced (e.g. a dkp-pacman upgrade).
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${library}")

	file(STRINGS "${library}" found_marker REGEX "${marker}" LIMIT_COUNT 1)
	if(NOT found_marker)
		message(FATAL_ERROR
			"${library} is the stock devkitPro build without PPSSPP's audio-thread hang fix.\n"
			"Run Switch/SDL2/build-patched-sdl2.sh inside the devkitA64 container, then reconfigure.")
	endif()
	message(STATUS "Found patched Switch SDL2 (${marker})")
endfunction()
