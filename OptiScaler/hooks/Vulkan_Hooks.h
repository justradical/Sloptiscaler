#pragma once
#include "SysUtils.h"

class VulkanHooks
{
  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkSignalSemaphore o_vkSignalSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    static void Hook(HMODULE vulkan1);
    static void Unhook();

    // ARM64EC + DXVK only: decides whether the DX11 overlay should take over the
    // present because the Vulkan overlay hooks are still not installed. Returns
    // false unchanged on non-ARM64EC builds.
    static bool TryUseDx11Overlay(bool isD3D11, bool willPresent);
};
