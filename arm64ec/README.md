# OptiScaler ARM64EC (Proton Experimental ARM64) port

Downstream patch set that builds OptiScaler as an **ARM64EC** PE DLL so it runs
natively inside FEX-emulated x86-64 games under Proton Experimental (ARM64),
instead of being emulated along with the game.

Not intended for upstreaming — everything lives in `patches/` as a git-format-patch
series against upstream `master`.

## Why ARM64EC

Verified on a Proton Experimental (ARM64) device: an emulated x86-64 process
loads an ARM64EC DLL, calls into it, and that code executes natively on the ARM
cores. Proton ships `libarm64ecfex.dll` (FEX's ARM64EC thunk layer), which is
what makes the x64 <-> ARM64EC boundary work.

## Toolchain

llvm-mingw (tested: 20260908, ucrt). Note that arm64ec is built as **ARM64X into
the aarch64 sysroot** -- there is no separate `arm64ec-w64-mingw32` sysroot dir,
only the `arm64ec-w64-mingw32-*` driver wrappers.

mingw-w64 already defines `_ARM64EC_`, `_M_ARM64EC` and `_AMD64_` for this
target, so Detours' ARM64EC paths activate correctly. Do **not** pass
`-D_MSC_VER`; it breaks mingw's own headers.

### Gotcha

clang defines `__x86_64__` for arm64ec (it uses the x64 ABI). Anything treating
`__x86_64__` as "SSE is available" breaks -- ImGui does, hence `-DIMGUI_DISABLE_SSE`.

## Layout

- `patches/`               git-format-patch series (apply with `git am`)
- `toolchain-arm64ec.cmake` CMake toolchain for cross-building deps
- `include-flags.txt`      include search path, `$DEPS` = dependency root

## Build state

- All 195 OptiScaler translation units compile clean for arm64ec.
- Dependencies rebuilt for arm64ec: **Detours**, **freetype**.
- Still x86-64 only: the FidelityFX static libs (`ffx_fsr2_*`, `ffx_fsr3*`,
  `ffx_backend_dx11_*`). x64 objects cannot be linked into an ARM64EC image, so
  these must be rebuilt from source before a full link.
- XeSS is `LoadLibrary`'d rather than linked, so it is not a build blocker; it
  simply will not work on ARM.

## Standard defines

```
-std=c++23 -DWIN32 -DNDEBUG -D_WINDOWS -D_USRDLL
-DIMGUI_DISABLE_SSE -DUNICODE -D_UNICODE -Wno-c++11-narrowing
-include compat/arm64ec/opti_sal_compat.h
```
