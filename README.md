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

### PS Vita (VitaGL)

The Vita build uses **VitaGL**, an OpenGL-compatible layer running on the Vita's
GXM GPU. It reuses the project's OpenGL scene compositor for the game: Sys24
background tiles, textured Model 2 geometry, then priority tiles. VitaGL is
initialized at 960x544 with 4x MSAA and a 32 MiB VRAM budget. SDL is used for
audio and timing only; VitaGL owns the display and buffer swaps.

Install VitaSDK and the target packages, then build with its cross toolchain:

```sh
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
vdpm install sdl2 vitaGL libpng zlib
cmake -S . -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita --target lift --parallel 4
```

CMake should report `segamod2: SDL2=ON PNG=TRUE GL=ON`. By default,
`build-vita/segamod2.vpk` starts the game directly after verifying and loading
the ROM set, without a launch menu. Install it with VitaShell. Supply
your complete, legally obtained **unpacked srallyc-b board dumps** under
`ux0:data/segamod2/ROMS/srallyc-b/` (not Daytona's ZIP layout). Existing ROM
verification and extraction are reused; generated caches go under
`ux0:data/segamod2/out/i960/`. No ROMs are packaged.

The direct-start build exits with an error if ROM verification or loading
fails. To build the optional launch-menu version, configure a separate build
directory with `-DSEGAMOD2_VITA_AUTOSTART=OFF`:

```sh
cmake -G Ninja -S . -B build-vita-autostart-ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSEGAMOD2_VITA_AUTOSTART=OFF
cmake --build build-vita-autostart-ninja --target lift-vpk --parallel 4
```

Extracted CPU/data caches are compared byte-for-byte with their source board
dumps at launch. Missing or mismatched caches are regenerated and checked
again. In launch-menu builds, I/O or verification failures return to the ROM
retry screen.

`ux0:data/segamod2/vita.log` is replaced at launch. NVRAM is stored in
`ux0:data/segamod2/nvram.yaml`, saved every five seconds, on pause/resume and
on shutdown. The existing YAML writer is non-atomic; use Save and Quit rather
than killing the application while it writes.

The VitaGL lifecycle follows the minimal Daytona backend: initialize a
960x544 surface with a 32 MiB VRAM budget and 4x MSAA, set the viewport, clear
the initial color/depth buffers, and present with `vglSwapBuffers(GL_FALSE)`.
Rally's launch and pause menus are rasterized into a CPU-side bitmap and
presented as a VitaGL texture quad. Startup markers go directly through native
Vita file I/O and to the kernel console, separately from C stderr. For a blank
screen in Vita3K, inspect the last `startup:` line in
`ux0:data/segamod2/vita.log`; compilation alone does not establish emulator
compatibility. Game-loop diagnostics bracket reset, geometry initialization,
viewer startup and frame presentation, and report scene/frame state without
claiming GPU timings.

The Vita frontend provides a visible Start/Resume, Reset, Options and Save and
Quit menu; settings include CPU and GPU clocks, volume, mute, steering dead
zone and inverted steering. Options persist in `ux0:data/segamod2/vita.cfg`
with a backup generation. Select=coin, Start=start, Triangle=test,
Circle=service, L/R=shift, left stick=steer, Cross=throttle, Square=brake;
Start+Select opens the pause menu.

Vita rendering is experimental and has **not** been validated on real hardware
or Vita3K as part of this change. Host tests cover CPU-side helpers and menu
behavior, not VitaGL shader compilation, GPU presentation or gameplay speed.
Screen recording remains disabled on Vita. Do not use the desktop build
directory for cross-compiling.

The Vita link reserves a page between executable and writable segments for
SCE metadata added by `vita-elf-create`; otherwise small code-size changes can
make SELF conversion fail with overlapping segments.

ROM-free host tests cover menu navigation, settings persistence and steering,
geometry helper behavior, tile composition, ROM cache validation, boot reset,
and runtime stack preservation. They do not exercise Vita hardware:

```sh
cmake -S . -B build -DSEGAMOD2_BUILD_VITA_TESTS=ON
cmake --build build --target segamod2_vita_tests segamod2_rom_cache_tests segamod2_boot_reset_tests segamod2_runtime_stack_tests segamod2_sys24_tile_tests segamod2_vita_menu_tests lift-check --parallel 4
ctest --test-dir build -R '^vita-' --output-on-failure
```
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
