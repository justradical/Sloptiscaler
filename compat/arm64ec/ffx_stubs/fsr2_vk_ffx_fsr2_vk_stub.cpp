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
#include <fsr2/ffx_fsr2.h>
#include <fsr2/vk/ffx_fsr2_vk.h>

FfxErrorCode ffxFsr2GetInterfaceVK(FfxFsr2Interface* outInterface, void* scratchBuffer, size_t scratchBufferSize, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr getDeviceProcAddr) { return FFX_ERROR_BACKEND_API_ERROR; }
size_t ffxFsr2GetScratchMemorySizeVK(VkPhysicalDevice physicalDevice) { return 0; }
FfxCommandList ffxGetCommandListVK(VkCommandBuffer cmdBuf) { return {}; }
FfxDevice ffxGetDeviceVK(VkDevice device) { return {}; }
FfxResource ffxGetTextureResourceVK(FfxFsr2Context* context, VkImage imgVk, VkImageView imageView, uint32_t width, uint32_t height, VkFormat imgFormat, const wchar_t* name, FfxResourceStates state) { return {}; }
