# m4xw fork breakages on non-Switch platforms

m4xw's Switch port made several changes to shared files that broke macOS/Linux/Windows builds. All fixed on `all-platforms-reintegration`. Consult this when merging future Switch-side work back to other platforms and a non-Switch build breaks — m4xw tended to add Switch-specific code unconditionally rather than behind platform guards.

| File | What m4xw did | Fix |
|---|---|---|
| `GPU/Common/GLCommon.h` | Unconditional Switch glsym includes in desktop GL branch | `#if PPSSPP_PLATFORM(SWITCH)` guard |
| `GPU/Debugger/GECommandTable.cpp` | `#include <strings.h>` (POSIX-only) | `#ifndef _WIN32` guard |
| `GPU/Common/GPUDebugInterface.cpp` | Same | Same |
| `Core/Util/PortManager.h` | miniupnp headers moved to `include/` subdir but paths not updated | Updated to `include/miniwget.h` etc. |
| `Core/MIPS/MIPSAsm.cpp` | armips v0.11 API: `StringList`→`vector<string>`, `wstring`→`string`, `getFileName()` returns `fs::path` | Updated to new API |
| `Core/Debugger/SymbolMap.cpp` | armips `LabelDefinition.name` is now `Identifier` not `wstring` | `Identifier(name)` |
| `CMakeLists.txt` | `GPU_VULKAN` removed from `GPU_IMPLS` entirely | Restored with `if(NOT USE_LIBNX)` guard |
| `CMakeLists.txt` | `LuaContext.cpp/h` commented out | Uncommented but guarded with `$<$<NOT:$<BOOL:${USE_LIBNX}>>:...>` generator expression — sol/config.hpp not present in Switch toolchain |
| `CMakeLists.txt` | Switch glsym sources added unconditionally to SDL builds | Moved inside `if(USE_LIBNX)` |
| `SDL/SDLGLGraphicsContext.cpp` | `#include <glsym/rglgen.h>` and call unconditional | `#if PPSSPP_PLATFORM(SWITCH)` guard |
| `Common/Thread/ThreadManager.cpp` | `th.detach()` commented out with "Critical mitigation, cant call detach" | Restored — libnx can't detach threads but Switch never reaches this code path (only Vulkan shader compilation uses DEDICATED_THREAD tasks, and Switch uses OpenGL) |
| `Common/GPU/OpenGL/GLCommon.h` line 6894 | `#include "../gfx_es2/gl3stub.h"` (non-existent path) | Changed to `"gl3stub.h"` (same directory) |

**Runtime crash fixed:** `ThreadManager::EnqueueTask` was calling `std::terminate()` on every Vulkan shader compile because the `std::thread` destructor fired on a joinable thread. This crashed all non-Switch platforms immediately on first game load.

**CMake gotcha for Switch:** New armips requires `-DARMIPS_USE_STD_FILESYSTEM=ON` — its bundled `ext/filesystem` (ghc) submodule isn't populated and is incompatible with libnx. GCC 15 on devkitA64 supports `std::filesystem` natively.
