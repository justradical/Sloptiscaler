#!/bin/bash
# Generate FSR2 DX12 shader permutation headers with AMD's FidelityFX_SC under Wine.
# SC emits DXIL byte arrays into C headers; DXIL is GPU bytecode, so the host CPU
# is irrelevant to the output -- this runs on the x86-64 build host and the result
# is linked into the ARM64EC target.
#
# Paths passed to SC must be RELATIVE: wine cannot resolve an absolute unix path
# like /home/... as a Windows path, and -I silently yields "Compilation Failed"
# for every permutation when it cannot find the includes.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${1:-$ROOT/arm64ec-deps}"
FSR2="$DEPS/FidelityFX-FSR2"
OUT="$DEPS/fsr2gen/dx12"
export WINEPREFIX="$DEPS/winepfx"
export WINEDEBUG=-all
mkdir -p "$OUT"

BASE=(-reflection -deps=gcc -DFFX_GPU=1
  -DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
  -E CS -Wno-for-redefinition -Wno-ambig-lit-shift -DFFX_HLSL=1 -DFFX_HLSL_6_2=1
  '-DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}'
  '-DFFX_FSR2_OPTION_HDR_COLOR_INPUT={0,1}'
  '-DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}'
  '-DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS={0,1}'
  '-DFFX_FSR2_OPTION_INVERTED_DEPTH={0,1}'
  '-DFFX_FSR2_OPTION_APPLY_SHARPENING={0,1}')
INC=../../src/ffx-fsr2-api/shaders

cd "$FSR2/tools/sc" || exit 1
sc() { wine FidelityFX_SC.exe "${BASE[@]}" "$@"; }

fail=0
for s in tcr_autogen autogen_reactive accumulate compute_luminance_pyramid \
         depth_clip lock reconstruct_previous_depth rcas; do
  b="ffx_fsr2_${s}_pass"; src="$INC/${b}.hlsl"
  echo "=== $b"
  sc -name="$b"        -DFFX_HALF=0 -T cs_6_2 -I $INC -output="$OUT" "$src" 2>&1 | tail -1
  sc -name="${b}_wave64" -DFFX_FSR2_PREFER_WAVE64="[WaveSize(64)]" -DFFX_HALF=0 -T cs_6_6 -I $INC -output="$OUT" "$src" 2>&1 | tail -1
  if [ "$s" != "compute_luminance_pyramid" ]; then
    sc -name="${b}_16bit"        -DFFX_HALF=1 -enable-16bit-types -T cs_6_2 -I $INC -output="$OUT" "$src" 2>&1 | tail -1
    sc -name="${b}_wave64_16bit" -DFFX_FSR2_PREFER_WAVE64="[WaveSize(64)]" -DFFX_HALF=1 -enable-16bit-types -T cs_6_6 -I $INC -output="$OUT" "$src" 2>&1 | tail -1
  fi
done
echo "=== permutation headers: $(ls "$OUT"/*_permutations.h 2>/dev/null | wc -l)  blobs: $(ls "$OUT"/*.h 2>/dev/null | wc -l)"
