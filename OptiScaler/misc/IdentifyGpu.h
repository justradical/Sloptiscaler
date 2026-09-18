#pragma once
#include <nvapi.h>
#include <proxies/D3D12_Proxy.h>
#include <device_info/device_info.hpp>

// vkd3d-proton
MIDL_INTERFACE("39da4e09-bd1c-4198-9fae-86bbe3be41fd")
ID3D12DXVKInteropDevice : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDXGIAdapter(REFIID iid, void** ppvObject) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetInstanceExtensions(UINT * pExtensionCount, const char** ppExtensions) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceExtensions(UINT * pExtensionCount, const char** ppExtensions) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceFeatures(const VkPhysicalDeviceFeatures2** ppFeatures) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanHandles(VkInstance * pVkInstance, VkPhysicalDevice * pVkPhysicalDevice,
                                                       VkDevice * pVkDevice) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanQueueInfo(ID3D12CommandQueue * pCommandQueue, VkQueue * pVkQueue,
                                                         UINT32 * pVkQueueFamily) = 0;

    virtual void STDMETHODCALLTYPE GetVulkanImageLayout(ID3D12Resource * pResource, D3D12_RESOURCE_STATES State,
                                                        VkImageLayout * pVkLayout) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanResourceInfo(ID3D12Resource * pResource, UINT64 * pVkHandle,
                                                            UINT64 * pBufferOffset) = 0;

    virtual HRESULT STDMETHODCALLTYPE LockCommandQueue(ID3D12CommandQueue * pCommandQueue) = 0;

    virtual HRESULT STDMETHODCALLTYPE UnlockCommandQueue(ID3D12CommandQueue * pCommandQueue) = 0;
};

// dxvk
MIDL_INTERFACE("3a6d8f2c-b0e8-4ab4-b4dc-4fd24891bfa5")
IDXGIVkInteropAdapter : public IUnknown
{
    virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance * pInstance, VkPhysicalDevice * pPhysDev) = 0;
};

MIDL_INTERFACE("4c5e1b0d-b0c8-4131-bfd8-9b2476f7f408")
IDXGIVkInteropFactory : public IUnknown
{
    virtual void STDMETHODCALLTYPE GetVulkanInstance(VkInstance * pInstance,
                                                     PFN_vkGetInstanceProcAddr * ppfnVkGetInstanceProcAddr) = 0;
};

struct GpuInformation
{
    LUID luid {}; // Unique id to be able to reference the exact GPU
    std::string name {};
    VendorId::Value vendorId = VendorId::Invalid;
    uint32_t deviceId = 0x0;
    uint32_t subsystemId = 0x0;
    uint32_t revisionId = 0x0;
    size_t dedicatedVramInBytes = 0;
    bool usesDxvk = false;
    bool usesVkd3dProton = false;
    bool softwareAdapter = false;
    std::filesystem::path driverStore {};

    // AMD
    bool fsr4ForcedSupport = false;
    FSR4Support fsr4Support {};
    FSR4Support realFsr4Support {};
    device_info::HwGeneration amdHwGeneration = device_info::HwGeneration::kUndefinedGeneration;

    // Nvidia
    bool dlssCapable = false;
    NV_GPU_ARCH_INFO nvidiaArchInfo {};
    bool noDisplayConnected = false;
};

inline constexpr bool IsEqualLUID(LUID luid1, LUID luid2)
{
    return luid1.HighPart == luid2.HighPart && luid1.LowPart == luid2.LowPart;
}

class IdentifyGpu
{
    inline static bool hasD3d12Capabilities = false;
    inline static std::mutex mutex {};
    inline static std::vector<GpuInformation> cache {};

    static std::vector<GpuInformation> checkGpuInfo();
    static void queryNvapi(GpuInformation& gpuInfo);

  public:
    static void getHardwareAdapter(IDXGIFactory* InFactory, IDXGIAdapter** InAdapter,
                                   D3D_FEATURE_LEVEL requiredFeatureLevel);

    // Sorted by priority, the first one should be treated as the primary one
    static std::vector<GpuInformation> getAllGpus();
    static GpuInformation getPrimaryGpu();
    static void updateD3d12Capabilities(D3d12Proxy::PFN_D3D12CreateDevice o_D3D12CreateDevice = nullptr);
    static void updateInt8Support(std::optional<bool>& sdkSupportsInt8, std::optional<bool>& amdxcffx64SupportsInt8);
};

// MSVC derives __uuidof() from the DECLSPEC_UUID/MIDL_INTERFACE attribute.
// mingw-w64 instead needs an explicit __CRT_UUID_DECL to specialise
// __mingw_uuidof<T>, otherwise every __uuidof() on these interfaces is an
// undefined reference at link time.
#if !defined(_MSC_VER) && defined(__CRT_UUID_DECL)
__CRT_UUID_DECL(ID3D12DXVKInteropDevice, 0x39da4e09, 0xbd1c, 0x4198, 0x9f, 0xae, 0x86, 0xbb, 0xe3, 0xbe, 0x41, 0xfd)
__CRT_UUID_DECL(IDXGIVkInteropAdapter, 0x3a6d8f2c, 0xb0e8, 0x4ab4, 0xb4, 0xdc, 0x4f, 0xd2, 0x48, 0x91, 0xbf, 0xa5)
__CRT_UUID_DECL(IDXGIVkInteropFactory, 0x4c5e1b0d, 0xb0c8, 0x4131, 0xbf, 0xd8, 0x9b, 0x24, 0x76, 0xf7, 0xf4, 0x08)
#endif
