# Sega Rally Championship (1995) — recomp

Host-compiled uplift of Sega Rally Championship (Model 2A / i960). This repo is the **game** tree: lifted C under `src/`, portable Model 2 runtime under `lib/model2/`.

## Build

Prerequisites: CMake 3.16+ and a C compiler. Viewer extras: **SDL2**, **libpng**, and OpenGL. Without SDL2/libpng the tree still links; viewer / PNG / GL features are compiled out.

```bash
cmake -B build
cmake --build build
```

That produces `build/segamod2` (`build/segamod2.exe` on Windows). On macOS/Linux, `make lift` still works: it configures CMake if needed, then builds the `lift` target.

```bash
cmake --build build --target lift          # compile + link
cmake --build build --target lift-check    # same (compile only)
cmake --build build --target lift-boot-viewer     # cold boot + SDL
cmake --build build --target lift-boot-practice   # skip to desert practice START
```

Running the binary with no arguments starts the SDL cold-boot viewer. Pass `--harness` for the short dispatch trace.

On first run (or if `out/i960/*.bin` are missing / wrong size), the host auto-extracts `maincpu_deinterleaved.bin` and `main_data_deinterleaved.bin` from board dumps under `ROMS/srallyc-b/` (or `SEGAMOD2_ROM_DIR`). No separate extract step is required. `scripts/extract_rom_blocks.py` / `cmake --build build --target rom-blocks` remain available if you want the bins without launching the game.

### macOS

```bash
brew install cmake libpng sdl2
cmake -B build && cmake --build build
```

### Windows

Use **Visual Studio 2022** (MSVC) with CMake. For the live viewer, install SDL2 and libpng via **vcpkg** (recommended):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sdl2:x64-windows libpng:x64-windows
```

Configure and build with the vcpkg toolchain:

```powershell
cd path\to\segarally95-recomp
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config RelWithDebInfo --target lift-check
```

Configure should report `SDL2=ON PNG=TRUE GL=ON`. Re-run `cmake -B build …` after installing packages if an earlier configure already ran without them.

**MSYS2 MinGW** (alternative toolchain — use a MinGW64 shell, not MSVC):

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-libpng
cmake -B build && cmake --build build
```

**Manual SDL2 SDK:** download the Visual C++ development libraries from [libsdl.org](https://github.com/libsdl-org/SDL/releases), then pass `-DSDL2_DIR=C:\path\to\SDL2\cmake`. You still need libpng separately for PNG dumps; OpenGL comes from Windows (`opengl32`).

### Optional tools

Coverage, `gen_lift_main`, stitch, and other RE helpers live in a separate tools repo. Clone into `./tools` only when you need those:

```bash
git clone git@github.com-xandoxan65:xandoxan65/segamodel2-tools.git tools
```

Then:

- `make lift-main` — regenerate `lift_main` / host invoke glue
- `make compare` / `make coverage` — ROM pipeline beyond extract
- `make sync-runtime` — pull canonical `tools/runtime` into `lib/model2`

### PS Vita (experimental native GXM)

The Vita build uses only **native GXM through libvita2d**, not an OpenGL
context. SDL2 is initialized for audio/timing only. The desktop frontend and
lifted game code are unchanged. The implementation was cross-checked against
[Daytona's GPU frontend](https://github.com/Jaikben/daytona-arcade-recomp/tree/9ad266b0a0d2b860c14d95cfe1425eea942d7b06/platform/vita):
GPU-visible indexed textures with immutable per-frame palettes, explicit
render-completion waits before recycling buffers, native controller polling,
writable data paths and separate host/ARM builds.

Use VitaSDK and its target packages. On a fresh SDK:

```sh
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
vdpm install sdl2 libvita2d libpng zlib
cmake -S . -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita --target lift --parallel 4
```

Configure should report `segamod2: SDL2=ON PNG=TRUE GL=OFF` for GXM. Install
`build-vita/segamod2.vpk` with VitaShell. Supply your complete, legally obtained
**unpacked srallyc-b board dumps** under
`ux0:data/segamod2/ROMS/srallyc-b/` (not Daytona's ZIP layout). The existing ROM
verification and extraction code is reused; generated caches go under
`ux0:data/segamod2/out/i960/`. No ROMs are packaged.

Extracted CPU/data caches are compared byte-for-byte with their source board
dumps at launch, not trusted solely by file size. Missing or mismatched caches
are regenerated and checked again before loading; I/O or verification failures
return to the ROM retry screen. This also recovers same-size stale/corrupt
caches that would otherwise leave splash catalogs unreadable.

`ux0:data/segamod2/vita.log` is replaced at launch. NVRAM is stored in
`ux0:data/segamod2/nvram.yaml`, saved every five seconds, on pause/resume and
on shutdown. Saves report write/close errors, but still use the existing
non-atomic YAML writer: unlike Daytona, there is no backup-generation recovery.
Use save and quit rather than killing the application while it writes.

The launch menu runs on the application thread before the large-stack guest
thread is created. Startup markers are written directly through native Vita
file I/O and to the kernel console, separately from C stderr, so a failure in
C logging, environment setup, GXM, font loading or guest-thread creation can
be localized. For a blank screen in Vita3K, inspect the last `startup:` line in
`ux0/data/segamod2/vita.log` inside the emulator's storage folder. If the log
is still empty, check Vita3K's console for `segamod2:` storage errors or an
executable-loader fault; successful compilation does not establish emulator
compatibility.

`startup: guest thread entered` confirms thread creation, not completion of
guest reset. Later markers bracket hardware reset, sound-board reset and
ROM/worker startup, boot environment setup, geometry initialization, NVRAM
loading, viewer opening, the initial frame and guest boot dispatch. If startup
stops, the last marker identifies the next operation to investigate; it does
not by itself establish the cause.

The diagnostic Vita build also writes limited `runtime:` probes during the
first 1,800 frames and game-mode transitions. Game-loop probes include the
boot phase/countdown and bracket the mode handler and vsync. GXM probes report
nonblack tile-pixel counts, projection/mesh availability and frame submission.
Reaching `entering post-reset dispatch` is normal: that loop does not return
during gameplay. For a black screen after SOUND INITIALIZE, capture the
`runtime:` lines after waiting at least 30 seconds; they distinguish a stalled
guest from empty scene data or submitted frames that the emulator does not
display. Triangle counts describe the input mesh, not necessarily visible
polygons.

Additional splash probes report the CGM header, batch/slot allocation, tile-map
and character data, pending palette uploads and layer registers. The first
three animated attract frames also bracket the inner-3 handler, GEO bootstrap,
script drawing and scene hook in the same application log. These probes do
not require access to Vita3K's emulator logs, which Android may restrict.
ROM checkpoints also record the splash bytes, data-buffer pointer/size and
mapped pointer after reset, geometry/viewer setup, boot and catalog drawing.
These distinguish changed ROM bytes from a changed address mapping.
The boot wait-table copy checks the full 32-bit `0xffffffff` terminator and
the 56-byte destination bound. A truncated terminator comparison previously
overwrote the adjacent ROM pointer/size in the Vita build, leaving splash and
geometry data inaccessible after SOUND INITIALIZE.
Tile caches update only changed map entries and referenced 32-byte glyphs;
scrolling and palette changes do not decode every layer again. Constant-depth
textured triangles use one triangle instead of 16 equivalent subdivisions.
Following Daytona's Vita renderer, perspective lattice vertices are evaluated
once and reused within each triangle. Varying-depth triangles 24 display
pixels or smaller skip subdivision (flat shading can't show perspective
warping at that size); larger ones keep at least 4x4 subdivision, and large
spans (over 320 display pixels) with reciprocal-depth ratios over 3 use 8x8
subdivision to reduce road texture warping. Real-hardware logs showed the
unconditional 4x4 floor amplifying a ~3,000-4,400 triangle race scene into
105,000-123,000 GPU vertices (present_us 16,000-36,000 vs. a 16,600 budget at
60fps); the small-triangle skip targets that without reducing detail on the
large/near polygons that actually need it. Allocation failures remain
explicit (pool exhaustion is not silently downgraded to lower detail).
Solid checkerboard shadows use a point-filtered repeating 2x2 mask anchored
to native screen coordinates, rather than half-alpha blending. Textured
checker polygons retain the existing approximation.
Source and per-frame material lookups use bounded hash tables with exact
collision checks. Consecutive compatible triangles share a draw call without
changing painter order, clipping, palette contents or perspective subdivision.
Painter order now uses precomputed keys and a stable radix sort, with insertion
sorting for at most 32 triangles. Hardware depth quantization, saturated-depth
float comparisons, world/overlay grouping and original-index ties are unchanged.
Two reusable sort arrays occupy about 256 KiB for 8,192 triangles (previously
32 KiB for one index array). Fully visible polygons bypass near/screen clipping,
and constant-depth triangles bypass perspective-bound calculations.
Indexed patches share one lazily allocated 32 MiB CDRAM arena instead of a
separate kernel allocation per texture; its budget includes padded row strides
and texture alignment. Arena recycling and release retain the GPU completion
fence. Material palettes are still rebuilt each frame to reflect palette/luma
changes. These adapt Daytona's caching, batching and arena patterns without
its deferred-material or quality-reducing policies.
Runtime GXM probes continue every 300 frames beyond the startup sampling
window, with interval FPS, CPU-side presentation time, geometry draw/vertex
counts, material lookup probes and source-arena usage. These are not GPU
timings; a log ending without an error/exit marker does not establish the
cause of an emulator termination.
`source_bytes` is occupied patch storage within the reserved 32 MiB arena,
not the total renderer memory allocation.

The host GXM test executable accepts `--benchmark` for a fixed 200-frame,
8,192-triangle constant-depth workload, `--benchmark-varying` for the same
workload with perspective subdivision, and `--snapshot <path>` for an ordered binary stream
of vertices, palettes, addressing and tint values. Snapshot streams can be
compared byte-for-byte between renderer versions, independently of batching.
The shim benchmark measures host CPU work, not Vita/Vita3K FPS or GPU time.
Against the preceding comparison-sort/clipping path with identical tessellation,
seven alternating runs measured median times of 0.369 to 0.175 seconds
(constant-depth) and 0.739 to 0.578 seconds (varying-depth) on the development
host. These are approximately 53% and 22% less CPU time for these synthetic
workloads, not gameplay speed claims. Ordered output snapshots matched
byte-for-byte; helper tests also compare randomized sort orders and 6,000
clipping cases against the preceding algorithms.

UI text uses a small built-in bitmap atlas drawn with native GXM. The application
does not load firmware PGF/PVF fonts or call `scePgf`/`scePvf`; this avoids the
PGF-loading stall observed in Vita3K. No external font files are required.

The Vita frontend uses a Daytona-style full-screen menu: white text on a dark
background, a yellow selected row, D-pad navigation and Cross to select.
The launch/pause menu offers **Start/Resume Game**, **Reset Game**, **Options**
and **Save and Quit**. Reset saves NVRAM, shuts down the viewer/audio/geometry
workers and cold-boots again; it does not erase cabinet settings.
Failed ROM loading returns to the launch menu so you can retry after fixing
the board dumps.

Options provide CPU clocks (111/222/333/444/500 MHz), GPU clocks
(41/77/111/166 MHz), volume, mute, steering dead zone (0–40%) and inverted
steering. Changes apply immediately and persist in
`ux0:data/segamod2/vita.cfg`; settings saves use a temporary file and retain a
`.bak` generation, which is loaded if the main settings file is missing or
invalid. Apply/save errors appear in the menu and log. Defaults retain the
existing full audio volume and stick dead zone, with 333 MHz CPU / 111 MHz GPU.
Reset Defaults restores these frontend settings, not NVRAM. GXM, fullscreen,
the SDL/SCSP audio engine, ROM layout and controller bindings remain fixed;
Daytona's alternative audio engine is not part of this project.

| Action | Vita control |
| --- | --- |
| Navigate frontend menus | D-pad up / down |
| Select highlighted entry | Cross |
| Change highlighted option | D-pad left / right (or Cross) |
| Return from Options / resume from main pause menu | Circle |
| Coin / cabinet start | Select / Start |
| Steer / accelerate / brake | Left stick / Cross / Square |
| Shift down / up | L / R (one shift per press, neutral through fourth gear) |
| Test / service | Triangle / Circle |
| Operator menu confirm / navigate | D-pad up / down, left or right |
| Open pause menu / resume | Start + Select |
| Reset / save and quit | Main menu entries |
| Enter test menu / service coin while paused | Options entries |

Pause-chord presses suppress cabinet inputs while both buttons are held;
Select pressed before the chord can still insert a coin. Release buttons after
startup or a pause transition to prevent UI presses leaking into the game.
Pause holds guest frame progression and pauses SDL audio playback; it does not
reset the existing sound-board thread or discard its bounded queued samples.

The GXM renderer consumes the existing camera-space triangles and palette/luma
helpers, clips at positive Z and the hardware viewport, and preserves the
bottom-tiles → polygons → priority-tiles order. It keeps the native 496x384
framebuffer proportions centered on 960x544. Texture sources are bounded to
32 MiB, with a 16 MiB transient GPU pool and a 128 MiB newlib heap; the guest
loop uses an explicit 1 MiB thread stack. Resource exhaustion is logged and
stops presentation rather than silently dropping triangles.

**Rendering is experimental, not parity- or full-speed-verified.** Like the
benchmark's libvita2d path, perspective is approximated using tessellation
(16 subtriangles per textured triangle). This implementation uses far-to-near
polygon ordering and alpha cutouts, not the desktop renderer's depth/stencil
phases. Checker polygons use half-alpha rather than an exact screen-space
checker mask. Intersection ordering, mirror seams, road perspective, memory
headroom, timing and complete races require real-Vita comparison. Screen
recording is disabled on Vita.

The Vita GL frontend and renderer-selection option have been removed. Ordinary
`sdl2` is required; configuration rejects `sdl2_vitagl` so the package does not
link vitaGL or its runtime shader compiler. No `libshacccg.suprx` is required.
If upgrading an SDK that used the old frontend, replace `sdl2_vitagl` with
`sdl2` using VitaSDK's package manager. Do not use the desktop build directory
for cross-compiling.
The Vita link reserves a page between executable and writable segments for
SCE metadata added by `vita-elf-create`; otherwise small code-size changes can
make SELF conversion fail with overlapping segments.

ROM-free host tests exercise menu navigation/actions, settings persistence and
backup recovery, steering options, menu layout/highlighting, clipping,
perspective attributes, controls, shared
polygon ordering, tile stride/channel conversion, tessellation counts, GPU
buffer lifetime and pool-failure handling. Software tile-compositor tests cover
all 256 palette banks (4,096 pens), guarded scroll/wrap, line scrolling,
priority and window masks. Boot-reset tests verify wait-table termination,
ROM-state preservation and rejection of an oversized table.
Repeated runtime-call tests also check stack preservation and formatter
argument/scratch safety across 10,000 iterations:

```sh
cmake -S . -B build -DSEGAMOD2_BUILD_VITA_TESTS=ON
cmake --build build --target segamod2_vita_tests segamod2_rom_cache_tests segamod2_boot_reset_tests segamod2_runtime_stack_tests segamod2_sys24_tile_tests segamod2_vita_menu_tests segamod2_vita_gxm_tests lift-check --parallel 4
ctest --test-dir build -R '^vita-' --output-on-failure
```

The renderer tests use a GXM/libvita2d shim: they do not execute ARM code,
validate actual shaders or prove hardware rendering/gameplay.

### ROMs

Place board dumps under `ROMS/srallyc-b/` (see `ROMS/README.md` and `ROMS/manifest.yaml`). ROM binaries are not committed.

## Layout

| Path | Role |
|------|------|
| `src/game`, `src/boot`, `src/libc`, `src/irq` | Uplifted game / libc |
| `src/host` | Game-side host bridges (viewer, CGM, NVRAM, …) |
| `src/lift_main.c`, `i960_host_*.c` | Committed entry + generated glue |
| `lib/model2/{geo,hw,tgp,snd,host}` | Portable Model 2 runtime |
| `lib/model2/include` | Shared host headers |
| `disasm/`, `symbols/`, `asm/` | Static RE artifacts |
| `scripts/extract_rom_blocks.py` | Minimal host ROM extract (no tools/) |
| `lib/host_compat/` | Windows pthread/time/GL loader (no-op on POSIX) |
| `tools/` | Optional clone of segamodel2-tools (gitignored) |

## License / ROMs

You must supply your own legally obtained ROM set. This repository does not distribute game binaries.
