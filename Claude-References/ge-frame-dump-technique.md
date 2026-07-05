# GE frame dump technique — ground truth for rendering bugs

When a rendering bug's exact cause is unclear after 1-2 rounds of log-based guessing, stop guessing and get a `.ppdmp` GE frame dump instead — it's ground truth (the literal draw calls/vertex data sent by the game), not inference from filtered logs.

**How to capture:** PPSSPP has a built-in GE (GPU) frame-dump feature — File > "Dump next frame to file" in the UI. Produces a `.ppdmp` file (magic `PPSSPPGE`, format documented in `GPU/Debugger/RecordFormat.h`) in the game's `SYSTEM/DUMP/` folder. Records the exact GE command stream (register writes, vertex/index buffers, textures, CLUTs) for one frame, compressed with zstd per-block.

**Two ways to read it:**
1. **Interactive**: the Windows build has a GE Debugger (`Windows/GEDebugger/`) that loads a `.ppdmp` directly and lets you step draw calls, inspect vertex data, and preview the framebuffer per-draw. Requires a human driving the GUI — not scriptable by the agent.
2. **Programmatic**: write a small Python parser (zstandard + struct) that decompresses the two blocks (`Command` array + raw pushbuf), walks `REGISTERS` blocks decoding PSP GE opcodes (`GE_CMD_VERTEXTYPE=0x12`, `GE_CMD_PRIM=0x4`), matches each `PRIM` draw call to its corresponding `VERTICES`/`INDICES` command (both lists are in emission order, so a simple running index works), and decodes vertex bytes per the PSP `vType` bitfield layout (texcoord/color/normal/position, each field aligned to its own element size, final stride aligned to the largest field present). This can search every draw in a frame for a bounding box matching a visible widget and return its exact vertex list in under a minute.

**Headless replay dead end (don't retry):** feeding a `.ppdmp` to `PPSSPPHeadless.exe` directly (`PPSSPPHeadless.exe file.ppdmp -l --graphics=directx11`) loads the file (confirmed via `LoadReplay disc0:/data.ppdmp` in the log) but produces zero draw-call output and exits almost instantly, for reasons not diagnosed. The binary-parsing approach above is faster and more reliable anyway.

**Case study:** this technique conclusively proved a Ridge Racer 2 menu box-border bug was a single 4-vertex `TRIANGLE_STRIP` quad, not a composite of overlapping sprites as previously guessed from logs — leading straight to the real fix (see cherry-pick history for the SpriteBorderFix hand-port).
