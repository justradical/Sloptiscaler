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
#include <fsr31/ffx_fsr3.h>
#include <fsr31/dx11/ffx_dx11.h>

namespace Fsr31
{
FfxResourceDescription GetFfxResourceDescriptionDX11(ID3D11Resource* pResource) { return {}; }
FfxCommandList ffxGetCommandListDX11(ID3D11DeviceContext* deviceContext) { return {}; }
FfxDevice ffxGetDeviceDX11_Fsr31(ID3D11Device* device) { return {}; }
FfxErrorCode ffxGetInterfaceDX11(FfxInterface* backendInterface, FfxDevice device, void* scratchBuffer, size_t scratchBufferSize, uint32_t maxContexts) { return FFX_ERROR_BACKEND_API_ERROR; }
size_t ffxGetScratchMemorySizeDX11(size_t maxContexts) { return 0; }
}

