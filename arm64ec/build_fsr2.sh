#!/bin/bash
# Cross-build AMD's FidelityFX-FSR2 (standalone 2.2.1) for ARM64EC.
#
# OptiScaler normally links prebuilt x86-64 COFF static libs, which cannot go
# into an ARM64EC image. This builds the same API from source instead.
#
# Prerequisites, both landing under arm64ec-deps/ (gitignored):
#   git clone --depth 1 --branch v2.2.1 \
#       https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git arm64ec-deps/FidelityFX-FSR2
#   arm64ec/gen_fsr2_shaders.sh      # DXIL permutation headers, via wine
#
# Note the repo is GPUOpen-Effects, not GPUOpen-LibrariesAndSDKs; and this is the
# standalone FSR2, not the FSR2 inside external/FidelityFX-SDK. That one is the
# SDK 1.x rewrite built on FfxInterface, whereas OptiScaler's fsr2 backend wants
# the standalone FfxFsr2Interface API these version macros describe (2.2.1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${1:-$ROOT/arm64ec-deps}"
TC="$DEPS/llvm-mingw-20260908-ucrt-ubuntu-22.04-x86_64/bin"
CXX="$TC/arm64ec-w64-mingw32-clang++"
AR="$TC/llvm-ar"
F="$DEPS/FidelityFX-FSR2/src/ffx-fsr2-api"
GEN="$DEPS/fsr2gen/dx12"
OBJ="$DEPS/fsr2gen/obj"

[ -d "$GEN" ] || { echo "missing generated shaders: $GEN" >&2; exit 1; }
mkdir -p "$OBJ"

FLAGS=(-std=c++17 -O2 -DWIN32 -DNDEBUG -D_WINDOWS -DUNICODE -D_UNICODE
       -DFFX_CPU -Wno-macro-redefined
       -I"$F" -I"$F/dx12" -I"$GEN" -I"$DEPS/DirectXMath/Inc"
       -include "$ROOT/compat/arm64ec/ffx_fsr2_compat.h")

SRCS=("$F/ffx_assert.cpp" "$F/ffx_fsr2.cpp"
      "$F/dx12/ffx_fsr2_dx12.cpp" "$F/dx12/shaders/ffx_fsr2_shaders_dx12.cpp")

rm -f "$OBJ"/*.o
for s in "${SRCS[@]}"; do
    o="$OBJ/$(basename "${s%.cpp}").o"
    echo ">> $(basename "$s")"
    "$CXX" "${FLAGS[@]}" -c "$s" -o "$o"
done
"$AR" rcs "$OBJ/libffx_fsr2_arm64ec.a" "$OBJ"/*.o
echo "built: $OBJ/libffx_fsr2_arm64ec.a"
