PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website: https://www.ppsspp.org/

Discord: https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [the development page](https://www.ppsspp.org/docs/development/).  Help testing, investigating, or fixing is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our GitHub page](https://github.com/hrydgard/ppsspp).

For documentation of all kinds (usage, reference, development), see the [documentation on the main website](https://www.ppsspp.org/docs)

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

To download fresh development builds for Android, Windows and Mac, [go to the /devbuilds page](https://www.ppsspp.org/devbuilds)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

This fork
---------

This is a **Nintendo Switch-focused fork** of PPSSPP, based on [m4xw's Switch port](https://github.com/m4xw/ppsspp) of upstream PPSSPP 1.19.3. It compiles for and supports all of PPSSPP's original platforms (Windows, Linux, macOS, Android, iOS, UWP), and any feature or fix that can reasonably be implemented across all platforms will be — but Switch is the primary target, and issues affecting Switch specifically get priority. Where a fix or feature only makes sense on one platform (e.g. Switch's Home Screen forwarder), it's implemented there without holding up work elsewhere.

What's new in AM.1.20.5 (Under Development)
------------------------

Changes since forking from m4xw's Switch port (based on upstream 1.19.3):

- **[SWITCH] Switch Home Screen forwarders** — "Add to Switch Home Screen" creates a launchable NRO forwarder/icon for a specific game directly on the Switch's Home Menu, bypassing the Homebrew Menu. Also supports creating a generic forwarder that launches straight into PPSSPP's game list rather than a specific ROM.
- **[ALL] Localization completeness pass** — filled in missing translation keys across all UI strings; `en_US.ini` is now the complete reference against every source string, with confident translations added across the other 44 language files where possible.
- **[ALL] Killzone: Liberation shimmer/comb artifact fix** — unified FBO with lazy per-frame copy for `SplitFramebufferMargin` games, plus reduced `DIRTY_FRAMEBUF` overhead.
- **[ALL] LocoRoco edge-bleed fix** — added a `DisableSmart2DTexFiltering` compat.ini flag so the "Smart 2D Texture Filtering" setting can be forced off per-game (fixes the pause-screen icon border bleed in LocoRoco/LocoRoco 2/Midnight Carnival), plus an `IsQuadPixelMapped()` guard fix for a related SpriteBorderFix regression affecting already pixel-mapped quads.
- **[ALL] Sprite border fix (hand-ported)** — fixes GTA Vice City Stories/Liberty City Stories edge bleed and the Ridge Racer menu box artifact, with an added compat flag and RECT-primitive support.
- **[ALL] FMV playback fixes** — fixed premature FMV cutoff (Ridge Racer 2 intro and others), MPEG ringbuffer lockup at end of video, and truncated-Access-Unit frame handling.
- **[ALL] Mappable log verbosity override control**, plus a "Disabled" verbosity mode and a Compatibility dev tools tab.
- **[ALL] GPU/rendering correctness fixes**: accurate `vdot` instruction, corrected depth clear translation, VFPU dot rounding overflow fix, `fsMinmaxDiscard` fallback for PSP min/max Z depth clipping.
- **[ALL] Audio fixes**: `setAudioStream` and multi-stream audio handling, MediaEngine AVIO callback bug with empty buffers.
- **[ALL] Crash fixes**: non-Switch thread-detach regression.
- **[ALL] Fixed file logging not working at all** — the log file path wasn't being stored until File output was already enabled, so enabling File logging produced no log file and nothing was ever written.
- **[ALL] Main-menu game-list controller navigation improvements** — default focus now lands on the current tab's first game icon (not the tab strip) on the first D-pad or face-button press; face buttons (A/B/X/Y) can now claim initial focus too, without also activating the highlighted game; switching tabs with L1/R1 moves focus sensibly to the new tab's first item when a game icon was previously focused.
- **[ALL] Larger, easier-to-tap Save/Load State buttons** — on screens with enough horizontal room (e.g. Switch handheld/docked, wide windows), the pause screen's Save State/Load State buttons now stretch to the full height of their save-slot row and sit beside the date, instead of small stacked buttons; falls back to the original compact layout when space is tight, and switches live if the window is resized.
- **[ALL] Controller/input**: both analog sticks shown in calibration view, PRESSURE/SIZE axes for right-stick diagonal mapping — lets you map digital buttons (e.g. ABXY) to the right stick with diagonal support, beneficial for games like Age of Zombies and Super Stardust Delta.
- **[ALL] Rounded UI corners** — buttons, list rows, popups, headers, and panel backgrounds now render with subtle rounded corners instead of hard right angles, for a softer, more polished look; degrades gracefully to a smaller radius on very small elements rather than clipping.
- **[ALL] Pause-menu exit option for auto-loaded ROMs** — new System settings option controls what the pause menu offers when PPSSPP is launched directly into a specific ROM (Switch Home Screen forwarder, Windows CLI/file-association launch, etc.): "Exit to Menu" (default), "Exit PPSSPP", or "None" (hides the item entirely for a console-style experience). The `--pause-menu-exit` CLI flag still overrides it when passed. Normal game-browser launches are unaffected.
- **[SWITCH] Fixed crash when launching another homebrew app after exiting PPSSPP** — exiting PPSSPP no longer breaks the next app launched from the Homebrew Menu.
- **[ALL] Full CI pipeline** covering Windows (x64/ARM64), Linux, macOS (universal), Android (incl. Quest VR), iOS, UWP, and Switch NRO builds, with unit + headless test jobs.
- **[ALL]** Assorted build fixes to keep every platform compiling cleanly on this branch, and misc. compat.ini/credits/QoL fixes.

What's new in 1.19.3
--------------------

This is the last release of upstream PPSSPP ([hrydgard/ppsspp](https://github.com/hrydgard/ppsspp)) that this fork was based on before diverging with the Switch-focused work above. See [upstream's release notes](https://github.com/hrydgard/ppsspp/releases/tag/v1.19.3) for what changed in that release.
