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
#include <fsr3/ffx_fsr3.h>

Fsr3::FfxErrorCode ffxFsr3ContextCreate(FfxFsr3Context* context, FfxFsr3ContextDescription* contextDescription) { return Fsr3::FFX_ERROR_BACKEND_API_ERROR; }
Fsr3::FfxErrorCode ffxFsr3ContextDestroy(FfxFsr3Context* context) { return Fsr3::FFX_ERROR_BACKEND_API_ERROR; }
Fsr3::FfxErrorCode ffxFsr3ContextDispatchUpscale(FfxFsr3Context* context, const FfxFsr3DispatchUpscaleDescription* dispatchParams) { return Fsr3::FFX_ERROR_BACKEND_API_ERROR; }
