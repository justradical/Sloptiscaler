#!/bin/bash
# Compile the SGSR2 passes to SPIR-V for the Vulkan backend.
#
# The shader text lives in SGSR2_Shaders.h as string literals, because the D3D12
# backend compiles it at runtime with D3DCompile. Rather than keep a second copy
# of the same HLSL for Vulkan -- two copies of a shader drift, and this one took
# a long time to get right -- the text is extracted from that header by a tiny
# host program which simply prints the very strings D3D12 compiles. Both APIs
# are therefore guaranteed to be running the same source.
#
# The Vulkan binding model comes from [[vk::binding]] attributes inside the HLSL,
# guarded by #ifdef VK_MODE so D3DCompile (which never defines it) is unaffected.
# This mirrors OptiScaler's own shaders/rcas precompile step, and uses the same
# vendored dxc, whose output was verified to reproduce the committed RCAS SPIR-V
# byte for byte.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${1:-$ROOT/arm64ec-deps}"
SRC="$ROOT/OptiScaler/upscalers/sgsr2"
OUT="$SRC/precompile"
TOOLS="$ROOT/OptiScaler/shaders/shader_tools"
WORK="$DEPS/sgsr2spv"
export WINEPREFIX="$DEPS/winepfx"
export WINEDEBUG=-all
mkdir -p "$OUT" "$WORK"

# 1. Extract the shader text straight out of the header.
cat > "$WORK/dump.cpp" <<'CPP'
#include "SGSR2_Shaders.h"
#include <cstdio>
static void write(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fputs(text, f);
    fclose(f);
}
int main(int argc, char** argv)
{
    if (argc < 3) return 1;
    write(argv[1], SGSR2_ConvertShader);
    write(argv[2], SGSR2_UpscaleShader);
    return 0;
}
CPP
c++ -std=c++17 -I"$SRC" -o "$WORK/dump" "$WORK/dump.cpp"
"$WORK/dump" "$WORK/sgsr2_convert.hlsl" "$WORK/sgsr2_upscale.hlsl"

# 2. HLSL -> SPIR-V. cs_6_0 rather than the cs_5_0 the D3D12 path uses at
#    runtime: SPIR-V generation needs a DXIL-era profile, and there is no
#    Wine d3dcompiler in the way here to constrain it.
for pass in convert upscale; do
    ( cd "$TOOLS" && wine dxc.exe -spirv -T cs_6_0 -E CSMain -O3 -Qstrip_debug \
        -D VK_MODE -Fo "$WORK/sgsr2_${pass}.spv" "$WORK/sgsr2_${pass}.hlsl" )
    if command -v spirv-val >/dev/null 2>&1; then
        spirv-val "$WORK/sgsr2_${pass}.spv"
    fi
    python3 "$TOOLS/create_header.py" "$WORK/sgsr2_${pass}.spv" \
        "$OUT/SGSR2_${pass}_Vk.h" "sgsr2_${pass}_spv"
done

echo "SPIR-V written to $OUT"
