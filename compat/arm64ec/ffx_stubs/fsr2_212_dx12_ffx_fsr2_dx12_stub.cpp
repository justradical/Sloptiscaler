/*
 * FidelityFX link stub -- TEMPORARY SCAFFOLDING, NOT A WORKING FSR BACKEND.
 *
 * OptiScaler statically links prebuilt FidelityFX libraries, which ship as
 * x86-64 COFF only; x64 objects cannot be linked into an ARM64EC image. Until
 * FidelityFX is rebuilt from source for ARM64EC these stubs satisfy the linker
 * so the DLL builds and its load/hook path can be exercised on real hardware.
 *
 * Every entry point fails immediately, so the FSR backends report themselves
 * unavailable instead of being called into. Signatures come from OptiScaler's
 * own bundled FidelityFX headers, so the ABI matches. Those headers are
 * Copyright (c) Advanced Micro Devices, Inc. and MIT-licensed; only the
 * declarations are reproduced here, none of AMD's implementation.
 *
 * One file per FidelityFX header: the fsr2, fsr2_212 and fsr31 headers declare
 * same-named functions with different signatures and cannot share a TU.
 */
#include "pch.h"
#include <fsr2_212/ffx_fsr2.h>
#include <fsr2_212/dx12/ffx_fsr2_dx12.h>

namespace Fsr212
{
FfxErrorCode ffxFsr2GetInterfaceDX12_212(FfxFsr2Interface212* fsr2Interface, ID3D12Device* device, void* scratchBuffer, size_t scratchBufferSize) { return FFX_ERROR_BACKEND_API_ERROR; }
size_t ffxFsr2GetScratchMemorySizeDX12_212() { return 0; }
FfxCommandList ffxGetCommandListDX12_212(ID3D12CommandList* cmdList) { return {}; }
FfxDevice ffxGetDeviceDX12_212(ID3D12Device* device) { return {}; }
FfxResource ffxGetResourceDX12_212(FfxFsr2Context* context, ID3D12Resource* resDx12, const wchar_t* name, FfxResourceStates state, UINT shaderComponentMapping) { return {}; }
}

