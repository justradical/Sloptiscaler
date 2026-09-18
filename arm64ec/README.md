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

- All OptiScaler translation units compile clean for arm64ec, and the DLL links.
- Verified on device: it loads into an emulated x86-64 process, completes init,
  attaches its Vulkan hook (so Detours hooking works under Wine+FEX) and unloads.
- Dependencies rebuilt for arm64ec: **Detours**, **freetype**. The vulkan-1 and
  `D3D12GetInterface` import libraries are synthesised at build time.
- **SGSR2** is added as a working D3D12 upscaler backend -- see below.
- **FSR does not work.** The FidelityFX static libs are x86-64 only and are
  currently satisfied by generated stubs in `compat/arm64ec/ffx_stubs/` that fail
  every call. Replacing them needs FidelityFX rebuilt from source for arm64ec,
  which in turn needs its shader compiler cross-built for Linux.
- XeSS and DLSS are `LoadLibrary`'d rather than linked, so they were never build
  blockers; they simply cannot work on ARM.

## SGSR2

Snapdragon Game Super Resolution 2 is the upscaler that actually makes sense
here: it ships as shader source only (BSD-3-Clause), so there is no x86-64 blob
to port, and it is Qualcomm's own upscaler for the Adreno GPU this targets.

`OptiScaler/upscalers/sgsr2/` holds an HLSL port of the reference GLSL 2-pass
compute variant plus a D3D12 backend. Select it with `upscaler=sgsr2`.

Validated so far: the shaders compile with Wine's d3dcompiler at cs_5_0 on the
target device, and the DLL builds and loads with the backend registered. The
upscaling path itself has **not** been exercised in a real game yet -- in
particular the motion-vector sign/scale convention (`motionVectorScale` in the
Convert pass) is derived rather than measured, and is the first thing to check
if output looks smeared or inverted.

## Standard defines

```
-std=c++23 -DWIN32 -DNDEBUG -D_WINDOWS -D_USRDLL
-DIMGUI_DISABLE_SSE -DUNICODE -D_UNICODE -Wno-c++11-narrowing
-include compat/arm64ec/opti_sal_compat.h
```
