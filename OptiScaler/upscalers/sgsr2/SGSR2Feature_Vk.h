#pragma once

#include "nvsdk_ngx_vk.h"

#include "SGSR2Feature.h"
#include <upscalers/IFeature_Vk.h>

// Vulkan backend for SGSR2.
//
// The shaders are the same HLSL the D3D12 backend runs; they are compiled to
// SPIR-V ahead of time by arm64ec/gen_sgsr2_spirv.sh, which extracts the text
// from SGSR2_Shaders.h so the two APIs cannot drift apart. The Vulkan binding
// numbers come from [[vk::binding]] attributes in that HLSL, guarded by
// VK_MODE, and the layout below has to match them exactly.
class SGSR2FeatureVk : public SGSR2Feature, public IFeature_Vk
{
  public:
    SGSR2FeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~SGSR2FeatureVk();

    Upscaler GetUpscalerType() const final { return Upscaler::SGSR2; }

  protected:
    bool InitInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  private:
    // Descriptor set 0, mirroring the [[vk::binding]] numbers in the HLSL.
    enum Binding : uint32_t
    {
        B_Constants = 0,
        B_SamplerPoint = 1,
        B_SamplerLinear = 2,
        B_Srv0 = 3, // Convert: colour   Upscale: previous history
        B_Srv1 = 4, //          depth             motion/depth/clip/alpha
        B_Srv2 = 5, //          velocity          YCoCg
        B_Srv3 = 6, //          (unused)          reactive mask
        B_Uav0 = 7, //          motion/depth/clip/alpha   scene colour
        B_Uav1 = 8, //          YCoCg                     next history
        B_Uav2 = 9, //          motion counter            (unused)
        B_Count = 10
    };

    // Descriptors are rewritten every frame, so the sets are ringed to avoid
    // touching one the GPU may still be reading.
    static constexpr uint32_t FrameRingDepth = 4;
    static constexpr uint32_t PassCount = 2;

    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    bool CreatePipelines();
    bool CreateResources();
    bool CreateImage(Image& img, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, const char* name);
    void DestroyImage(Image& img);
    void Barrier(VkCommandBuffer cmd, Image& img, VkImageLayout newLayout, VkAccessFlags srcAccess,
                 VkAccessFlags dstAccess);
    void WriteDescriptors(VkDescriptorSet set, const VkDescriptorImageInfo srvs[4],
                          const VkDescriptorImageInfo uavs[2], VkBuffer constants, VkDeviceSize constantOffset);
    bool DispatchPasses(VkCommandBuffer cmd, NVSDK_NGX_Resource_VK* color, NVSDK_NGX_Resource_VK* velocity,
                        NVSDK_NGX_Resource_VK* depth, NVSDK_NGX_Resource_VK* output,
                        NVSDK_NGX_Resource_VK* reactive);

    VkDescriptorSetLayout _setLayout = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _convertPipeline = VK_NULL_HANDLE;
    VkPipeline _upscalePipeline = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets; // FrameRingDepth * PassCount

    VkSampler _samplerPoint = VK_NULL_HANDLE;
    VkSampler _samplerLinear = VK_NULL_HANDLE;

    VkBuffer _constantBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _constantMemory = VK_NULL_HANDLE;
    uint8_t* _constantMapped = nullptr;
    VkDeviceSize _constantStride = 0;

    // RWBuffer<uint> in HLSL is a storage *texel* buffer in SPIR-V, so it needs
    // a buffer view rather than a plain descriptor.
    VkBuffer _motionCounter = VK_NULL_HANDLE;
    VkDeviceMemory _motionCounterMemory = VK_NULL_HANDLE;
    VkBufferView _motionCounterView = VK_NULL_HANDLE;

    Image _motionDepthClipAlpha {};
    Image _ycocg {};
    Image _history[2] {};
    Image _output {};

    uint32_t _ringIndex = 0;
    uint32_t _historyIndex = 0;
    bool _loggedNoDepth = false;
    bool _loggedReactive = false;
};
