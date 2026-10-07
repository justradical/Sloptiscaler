#pragma once

// ARM64EC DXVK overlay route selector.
//
// On ARM64EC under Wine + DXVK the DX11 overlay is the only path that can draw
// over the game, because the process presents through a D3D11 swapchain that
// DXVK backs with Vulkan, and the PE-side Vulkan presentation hooks
// (vkCreateSwapchainKHR / vkQueuePresentKHR) are not always reached. However the
// DX11 overlay may only be used while the Vulkan overlay path has not taken over
// the present chain: drawing from both would put a second renderer into the one
// ImGui context, and choosing the wrong path simply makes the overlay disappear.
//
// This class is therefore a pure predicate over the runtime facts and owns
// nothing but its own boolean. It performs no locking, no device access and no
// installation; callers serialise deliberate selection/installation under the
// already existing hook mutex (see VulkanHooks::TryUseDx11Overlay / HookDevice).
//
// Standard library only: no Windows headers, no Vulkan, no ImGui, no threads.

#include <atomic>

namespace OptiOverlayRoute
{

class Dx11Fallback
{
  public:
    Dx11Fallback() = default;

    // Evaluates all seven guards on every call.
    //
    // The six positive predicates (arm64ec, wine, dxvk, dx11, willPresent,
    // overlayEnabled) must all hold and no Vulkan hooks may be installed. A
    // rejection never revokes an existing selection: selection is sticky until
    // Reset() so a late, unrelated device probe cannot steal the renderer from
    // the route that already won. A probe that satisfies every guard
    // re-confirms the selection and reports true.
    bool TrySelect(bool arm64ec, bool wine, bool dxvk, bool dx11, bool willPresent, bool overlayEnabled,
                   bool vulkanHooksInstalled)
    {
        const bool eligible = arm64ec && wine && dxvk && dx11 && willPresent && overlayEnabled &&
                              !vulkanHooksInstalled;

        if (eligible)
            _selected.store(true, std::memory_order_release);

        return eligible;
    }

    // Readable from the surface callback, which may run on another thread.
    bool IsSelected() const { return _selected.load(std::memory_order_acquire); }

    // Relinquishes ownership; normal shutdown (unhook) only.
    void Reset() { _selected.store(false, std::memory_order_release); }

  private:
    std::atomic<bool> _selected { false };
};

} // namespace OptiOverlayRoute
