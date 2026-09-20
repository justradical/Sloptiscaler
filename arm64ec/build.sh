#!/usr/bin/env bash
# Build OptiScaler as an ARM64EC PE DLL for Proton Experimental (ARM64).
#
#   ./arm64ec/build.sh [deps-dir]
#
# Fetches llvm-mingw + the header-only deps into deps-dir (default ./arm64ec-deps),
# builds Detours and freetype for arm64ec, then compiles and links OptiScaler.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="${1:-$ROOT/arm64ec-deps}"
OUT="$DEPS/build"
LLVM_MINGW_VER=20260908
LLVM_MINGW="$DEPS/llvm-mingw-${LLVM_MINGW_VER}-ucrt-ubuntu-22.04-x86_64"
TC="$LLVM_MINGW/bin"
CXX="$TC/arm64ec-w64-mingw32-clang++"

mkdir -p "$DEPS" "$OUT/obj"

# ---------------------------------------------------------------- toolchain
if [ ! -d "$LLVM_MINGW" ]; then
  echo ">> fetching llvm-mingw $LLVM_MINGW_VER"
  curl -sL -o "$DEPS/llvm-mingw.tar.xz" \
    "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VER}/llvm-mingw-${LLVM_MINGW_VER}-ucrt-ubuntu-22.04-x86_64.tar.xz"
  tar xf "$DEPS/llvm-mingw.tar.xz" -C "$DEPS"
fi

# ------------------------------------------------------------ header-only deps
# mingw-w64 ships only a stub directxmath.h, so use Microsoft's real one.
[ -d "$DEPS/DirectXMath" ] || git clone --depth 1 -q https://github.com/microsoft/DirectXMath.git "$DEPS/DirectXMath"
[ -d "$DEPS/Detours" ]     || git clone --depth 1 -q https://github.com/microsoft/Detours       "$DEPS/Detours"
[ -d "$DEPS/freetype" ]    || git clone --depth 1 -q -b VER-2-13-3 \
                                 https://gitlab.freedesktop.org/freetype/freetype.git "$DEPS/freetype"

# ------------------------------------------------- case-insensitivity shim
# OptiScaler uses MSVC-cased includes (<Windows.h>) and a few in-repo includes
# whose case does not match the file on disk. Harmless on Windows, fatal on a
# case-sensitive filesystem.
SHIM="$OUT/caseshim"; mkdir -p "$SHIM/precompile" "$SHIM/proxies" "$SHIM/resource_tracking"
MINC="$LLVM_MINGW/aarch64-w64-mingw32/include"
for h in Windows.h Unknwn.h Softpub.h WinTrust.h Xinput.h; do
  lc=$(echo "$h" | tr 'A-Z' 'a-z'); [ -e "$MINC/$lc" ] && ln -sf "$MINC/$lc" "$SHIM/$h"
done
ln -sf "$ROOT/OptiScaler/Config.h"                              "$SHIM/config.h"
ln -sf "$ROOT/OptiScaler/proxies/D3D12_Proxy.h"                 "$SHIM/proxies/D3d12_Proxy.h"
ln -sf "$ROOT/OptiScaler/proxies/Dxgi_Proxy.h"                  "$SHIM/proxies/DXGI_Proxy.h"
ln -sf "$ROOT/OptiScaler/resource_tracking/ResTrack_dx12.h"     "$SHIM/resource_tracking/ResTrack_Dx12.h"
ln -sf "$ROOT/external/xess/inc/xess/xess_debug.h"              "$SHIM/xess_dbg.h"
for f in "$ROOT"/OptiScaler/shaders/*/precompile/*_Shader*.h; do
  b=$(basename "$f"); up=$(echo "$b" | sed -E 's/^([a-z]+)_/\U\1_/')
  [ "$up" != "$b" ] && ln -sf "$f" "$SHIM/precompile/$up"
done
printf '#pragma once\n#define VER_BUILD_DATE "%s"\n' "$(date +%Y-%m-%d)" > "$SHIM/resource_build_date.h"
printf '#pragma once\n#define VER_BUILD_COMMIT "%s"\n' "$(git -C "$ROOT" rev-parse --short HEAD)" > "$SHIM/resource_build_commit.h"

# ------------------------------------------------------------------ Detours
if [ ! -f "$OUT/libdetours.a" ]; then
  echo ">> building Detours (arm64ec)"
  # MSVC's 'ui64' literal suffix is not standard C++.
  sed -i 's/~0ui64/~0ULL/g' "$DEPS/Detours/src/detours.cpp"
  ( cd "$DEPS/Detours/src"
    for f in detours modules disasm image creatwth disolarm64; do
      "$CXX" -c "$f.cpp" -o "$OUT/obj/detours_$f.o" -I. -O2 -DWIN32_LEAN_AND_MEAN -Wno-everything
    done )
  "$TC/llvm-ar" rcs "$OUT/libdetours.a" "$OUT"/obj/detours_*.o
fi

# ----------------------------------------------------------------- freetype
if [ ! -f "$OUT/freetype/libfreetype.a" ]; then
  echo ">> building freetype (arm64ec)"
  LLVM_MINGW_ROOT="$LLVM_MINGW" cmake -S "$DEPS/freetype" -B "$OUT/freetype" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/arm64ec/toolchain-arm64ec.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON >/dev/null
  cmake --build "$OUT/freetype" -j"$(nproc)" >/dev/null
fi

# ------------------------------------------------------------- import libs
# mingw's libd3d12.a predates D3D12GetInterface.
if [ ! -f "$OUT/libd3d12_extra.a" ]; then
  printf 'LIBRARY d3d12.dll\nEXPORTS\nD3D12GetInterface\n' > "$OUT/d3d12_extra.def"
  "$TC/arm64ec-w64-mingw32-dlltool" -d "$OUT/d3d12_extra.def" -l "$OUT/libd3d12_extra.a" -m arm64ec
fi

# ------------------------------------------------------------------- compile
INC="-I$SHIM -I$DEPS/DirectXMath/Inc -I$ROOT/compat/arm64ec -I$ROOT/OptiScaler/include/imgui"
while read -r p; do [ -n "$p" ] && INC="$INC -I$ROOT/$p"; done < "$ROOT/arm64ec/include-flags.txt.paths"
DEFS="-DWIN32 -DNDEBUG -D_WINDOWS -D_USRDLL -DIMGUI_DISABLE_SSE -DUNICODE -D_UNICODE -Wno-c++11-narrowing"
FLAGS="-std=c++23 -O2 $INC -include $ROOT/compat/arm64ec/opti_sal_compat.h $DEFS"

# ------------------------------------------------------------------ FSR2 2.2.1
# OptiScaler normally links prebuilt FidelityFX static libs, which ship as x86-64
# COFF and cannot go into an ARM64EC image; compat/arm64ec/ffx_stubs stands in so
# the DLL still links. build_fsr2.sh cross-builds the real thing from source, and
# where it has produced a library the matching stubs are dropped.
#
# It needs the FSR2 source tree and the generated shader permutation headers
# (arm64ec/../arm64ec-deps/fsr2gen), so it stays optional: without them the build
# falls back to the stubs exactly as before.
FSR2_LIB="$DEPS/fsr2gen/obj/libffx_fsr2_arm64ec.a"
FSR2_STUBS_REPLACED=""
if [ -d "$DEPS/FidelityFX-FSR2" ] && [ -d "$DEPS/fsr2gen/dx12" ]; then
  echo ">> building FidelityFX-FSR2 2.2.1 for arm64ec"
  "$ROOT/arm64ec/build_fsr2.sh" "$DEPS" >/dev/null || { echo "   FSR2 build failed, falling back to stubs"; FSR2_LIB=""; }
fi
if [ -f "$FSR2_LIB" ]; then
  # Only the core and DX12 backend are built; the DX11 and Vulkan FSR2 stubs stay.
  FSR2_STUBS_REPLACED="-not -name fsr2_ffx_fsr2_stub.cpp -not -name fsr2_dx12_ffx_fsr2_dx12_stub.cpp"
else
  FSR2_LIB=""
fi

echo ">> compiling OptiScaler"
cd "$ROOT"
find OptiScaler -name '*.cpp' -not -path '*/library/*' -not -name 'imgui_impl_uwp.cpp' > "$OUT/srcs.txt"
find compat/arm64ec -name '*.cpp' $FSR2_STUBS_REPLACED >> "$OUT/srcs.txt"

# Drop objects whose source is no longer in the list. The link globs obj/*.o, so
# a stale object silently stays in the image -- which is not a theoretical worry:
# the FSR2 stubs above kept resolving after their sources were dropped, so the
# real library was never pulled out of the archive and the DLL came out byte-for-byte
# identical to the stub build.
awk '{gsub(/\//,"_"); sub(/\.cpp$/,".o"); print}' "$OUT/srcs.txt" | sort -u > "$OUT/expected_objs.txt"
if [ -d "$OUT/obj" ]; then
  find "$OUT/obj" -name '*.o' -printf '%f\n' | sort -u > "$OUT/actual_objs.txt"
  comm -13 "$OUT/expected_objs.txt" "$OUT/actual_objs.txt" | while read -r stale; do
    [ -n "$stale" ] && { echo "   pruning stale object: $stale"; rm -f "$OUT/obj/$stale"; }
  done
fi
xargs -P "$(nproc)" -I{} sh -c \
  "\"$CXX\" $FLAGS -c '{}' -o \"$OUT/obj/\$(echo '{}' | tr / _ | sed 's/\.cpp\$/.o/')\"" < "$OUT/srcs.txt"

# Version resource: the .rc is UTF-16LE, which llvm-rc cannot read.
iconv -f UTF-16LE -t UTF-8 OptiScaler/OptiScaler.rc -o "$OUT/OptiScaler_utf8.rc"
"$TC/arm64ec-w64-mingw32-windres" -I OptiScaler -I "$SHIM" -DOPTISCALER_BUILD_METADATA \
  "$OUT/OptiScaler_utf8.rc" -O coff -o "$OUT/obj/zz_resource.res" || true

# mingw ships no vulkan-1 import library; synthesise one from the vk_* symbols
# the objects actually reference.
if [ ! -f "$OUT/libvulkan-1.a" ]; then
  echo ">> generating vulkan-1 import library"
  { echo "LIBRARY vulkan-1.dll"; echo "EXPORTS"
    "$TC/llvm-nm" --undefined-only "$OUT"/obj/*.o | awk '{print $NF}' \
      | sed 's/^#//' | grep -E '^vk[A-Za-z0-9]+$' | sort -u
  } > "$OUT/vulkan-1.def"
  "$TC/arm64ec-w64-mingw32-dlltool" -d "$OUT/vulkan-1.def" -l "$OUT/libvulkan-1.a" -m arm64ec
fi

# ---------------------------------------------------------------------- link
# Source.def names undecorated targets, which MSVC's linker resolves but the
# mingw driver does not; remap each to the real mangled symbol.
echo ">> generating arm64ec export table"
"$TC/llvm-nm" --defined-only "$OUT"/obj/*.o | awk '$2=="T"{print $3}' | sed 's/^#//' | sort -u > "$OUT/all_syms.txt"
python3 "$ROOT/arm64ec/gen_def.py" OptiScaler/Source.def "$OUT/all_syms.txt" "$OUT/Source_arm64ec.def"

echo ">> linking"
"$CXX" -shared -o "$OUT/OptiScaler.dll" "$OUT"/obj/*.o "$OUT/obj/zz_resource.res" "$OUT/Source_arm64ec.def" \
  ${FSR2_LIB:+"$FSR2_LIB"} \
  -L"$OUT" -L"$OUT/freetype" -ldetours -lfreetype -lvulkan-1 -ld3d12_extra \
  -ld3d11 -ld3d12 -ldxgi -ldxguid -ld3dcompiler -lwinhttp -ldbghelp -ldwmapi \
  -lgdi32 -limm32 -lshell32 -luser32 -lversion -lole32 -loleaut32 -luuid \
  -static-libgcc -static-libstdc++ -Wl,--error-limit=0

echo
echo "built: $OUT/OptiScaler.dll"
"$TC/llvm-readobj" --file-headers "$OUT/OptiScaler.dll" | grep -i machine
