#include "pch.h"
#include "SGSR2Feature_Vk.h"
#include "precompile/SGSR2_convert_Vk.h"
#include "precompile/SGSR2_upscale_Vk.h"

#include <Config.h>
#include <Util.h>
#include <MathUtils.h>

#include <algorithm>
#include <cstring>

static constexpr uint32_t kTileSize = 8;
static inline uint32_t DivRoundUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

SGSR2FeatureVk::SGSR2FeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : SGSR2Feature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    SetInitParameters(InParameters);
}

static uint32_t FindMemoryType(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp {};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

bool SGSR2FeatureVk::CreateImage(Image& img, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                                 const char* name)
{
    img.format = fmt;
    img.width = w;
    img.height = h;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo ici { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = { w, h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(Device, &ici, nullptr, &img.image) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImage failed for {}", name);
        return false;
    }

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(Device, img.image, &req);

    VkMemoryAllocateInfo mai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(PhysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(Device, &mai, nullptr, &img.memory) != VK_SUCCESS)
    {
        LOG_ERROR("vkAllocateMemory failed for {}", name);
        return false;
    }
    vkBindImageMemory(Device, img.image, img.memory, 0);

    VkImageViewCreateInfo ivci { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivci.image = img.image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = fmt;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(Device, &ivci, nullptr, &img.view) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImageView failed for {}", name);
        return false;
    }

    return true;
}

void SGSR2FeatureVk::DestroyImage(Image& img)
{
    if (img.view != VK_NULL_HANDLE)
        vkDestroyImageView(Device, img.view, nullptr);
    if (img.image != VK_NULL_HANDLE)
        vkDestroyImage(Device, img.image, nullptr);
    if (img.memory != VK_NULL_HANDLE)
        vkFreeMemory(Device, img.memory, nullptr);
    img = {};
}

void SGSR2FeatureVk::Barrier(VkCommandBuffer cmd, Image& img, VkImageLayout newLayout, VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess)
{
    // Deliberately no same-layout early-out. Our images sit permanently in
    // GENERAL, so skipping those barriers would emit nothing at all from the
    // second frame onwards -- and the hazards here are across frames, not
    // within one: pass 2 writes nextHistory, and the next frame's pass 2 samples
    // that same image as prevHistory. A GENERAL->GENERAL barrier is legal and is
    // exactly the tool for that. The D3D12 backend gets this for free because it
    // restores resource states at end of frame.
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = img.layout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    img.layout = newLayout;
}

bool SGSR2FeatureVk::CreatePipelines()
{
    // One layout for both passes, exactly as the D3D12 backend uses one root
    // signature for both. Bindings a given pass does not reference are still
    // declared: dxc strips unused ones from each SPIR-V module, and Vulkan is
    // happy for a layout to describe more than a shader uses.
    VkDescriptorSetLayoutBinding bindings[B_Count] {};
    auto set = [&](uint32_t b, VkDescriptorType t)
    {
        bindings[b].binding = b;
        bindings[b].descriptorType = t;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    };
    set(B_Constants, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    set(B_SamplerPoint, VK_DESCRIPTOR_TYPE_SAMPLER);
    set(B_SamplerLinear, VK_DESCRIPTOR_TYPE_SAMPLER);
    for (uint32_t b = B_Srv0; b <= B_Srv3; b++)
        set(b, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    set(B_Uav0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    set(B_Uav1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    set(B_Uav2, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

    VkDescriptorSetLayoutCreateInfo slci { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    slci.bindingCount = B_Count;
    slci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(Device, &slci, nullptr, &_setLayout) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateDescriptorSetLayout failed");
        return false;
    }

    VkPipelineLayoutCreateInfo plci { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &_setLayout;
    if (vkCreatePipelineLayout(Device, &plci, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreatePipelineLayout failed");
        return false;
    }

    struct
    {
        const unsigned char* spv;
        size_t size;
        const char* name;
        VkPipeline* target;
    } passes[] = {
        { sgsr2_convert_spv, sizeof(sgsr2_convert_spv), "SGSR2_Convert", &_convertPipeline },
        { sgsr2_upscale_spv, sizeof(sgsr2_upscale_spv), "SGSR2_Upscale", &_upscalePipeline },
    };

    for (auto& pass : passes)
    {
        VkShaderModuleCreateInfo smci { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = pass.size;
        smci.pCode = reinterpret_cast<const uint32_t*>(pass.spv);

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &smci, nullptr, &module) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateShaderModule failed for {}", pass.name);
            return false;
        }

        VkComputePipelineCreateInfo cpci { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = module;
        cpci.stage.pName = "CSMain"; // dxc keeps the HLSL entry point name
        cpci.layout = _pipelineLayout;

        auto res = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &cpci, nullptr, pass.target);
        vkDestroyShaderModule(Device, module, nullptr);

        if (res != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateComputePipelines failed for {}: {}", pass.name, (int) res);
            return false;
        }
    }

    return true;
}

bool SGSR2FeatureVk::CreateResources()
{
    const uint32_t rw = RenderWidth(), rh = RenderHeight();
    // TargetWidth, not DisplayWidth: UpdateSharedConstants writes displaySize
    // from TargetWidth, and the upscale pass bounds-checks against it. They are
    // equal until output scaling moves the target, and must not disagree then.
    const uint32_t dw = TargetWidth(), dh = TargetHeight();

    const VkImageUsageFlags rwUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (!CreateImage(_motionDepthClipAlpha, rw, rh, VK_FORMAT_R16G16B16A16_SFLOAT, rwUsage, "MotionDepthClipAlpha") ||
        !CreateImage(_ycocg, rw, rh, VK_FORMAT_R32_UINT, rwUsage, "YCoCg") ||
        !CreateImage(_history[0], dw, dh, VK_FORMAT_R16G16B16A16_SFLOAT, rwUsage, "History0") ||
        !CreateImage(_history[1], dw, dh, VK_FORMAT_R16G16B16A16_SFLOAT, rwUsage, "History1") ||
        !CreateImage(_output, dw, dh, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "Output"))
        return false;

    VkSamplerCreateInfo sci { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = VK_LOD_CLAMP_NONE;

    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(Device, &sci, nullptr, &_samplerPoint) != VK_SUCCESS)
        return false;

    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(Device, &sci, nullptr, &_samplerLinear) != VK_SUCCESS)
        return false;

    // Constants, ringed and persistently mapped.
    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties(PhysicalDevice, &props);
    const VkDeviceSize align = std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 16);
    _constantStride = (sizeof(SGSR2Constants) + align - 1) & ~(align - 1);

    VkBufferCreateInfo bci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = _constantStride * FrameRingDepth;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Device, &bci, nullptr, &_constantBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req {};
    vkGetBufferMemoryRequirements(Device, _constantBuffer, &req);
    VkMemoryAllocateInfo mai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(PhysicalDevice, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(Device, &mai, nullptr, &_constantMemory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(Device, _constantBuffer, _constantMemory, 0);
    if (vkMapMemory(Device, _constantMemory, 0, bci.size, 0, (void**) &_constantMapped) != VK_SUCCESS)
        return false;

    // Motion counter: a single uint, addressed as a storage texel buffer
    // because RWBuffer<uint> compiles to an image-buffer in SPIR-V.
    VkBufferCreateInfo mc { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    mc.size = sizeof(uint32_t);
    mc.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    mc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Device, &mc, nullptr, &_motionCounter) != VK_SUCCESS)
        return false;

    vkGetBufferMemoryRequirements(Device, _motionCounter, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(PhysicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(Device, &mai, nullptr, &_motionCounterMemory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(Device, _motionCounter, _motionCounterMemory, 0);

    VkBufferViewCreateInfo bvci { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
    bvci.buffer = _motionCounter;
    bvci.format = VK_FORMAT_R32_UINT;
    bvci.range = VK_WHOLE_SIZE;
    if (vkCreateBufferView(Device, &bvci, nullptr, &_motionCounterView) != VK_SUCCESS)
        return false;

    // Descriptor pool sized for the whole ring, both passes.
    const uint32_t sets = FrameRingDepth * PassCount;
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets },
        { VK_DESCRIPTOR_TYPE_SAMPLER, sets * 2 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sets * 4 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, sets },
    };
    VkDescriptorPoolCreateInfo dpci { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = sets;
    dpci.poolSizeCount = (uint32_t) std::size(sizes);
    dpci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(Device, &dpci, nullptr, &_descriptorPool) != VK_SUCCESS)
        return false;

    std::vector<VkDescriptorSetLayout> layouts(sets, _setLayout);
    VkDescriptorSetAllocateInfo dsai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = _descriptorPool;
    dsai.descriptorSetCount = sets;
    dsai.pSetLayouts = layouts.data();
    _descriptorSets.resize(sets);
    if (vkAllocateDescriptorSets(Device, &dsai, _descriptorSets.data()) != VK_SUCCESS)
        return false;

    return true;
}

void SGSR2FeatureVk::WriteDescriptors(VkDescriptorSet set, const VkDescriptorImageInfo srvs[4],
                                      const VkDescriptorImageInfo uavs[2], VkBuffer constants,
                                      VkDeviceSize constantOffset)
{
    VkDescriptorBufferInfo cb { constants, constantOffset, sizeof(SGSR2Constants) };
    VkDescriptorImageInfo pointInfo { _samplerPoint, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
    VkDescriptorImageInfo linearInfo { _samplerLinear, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };

    VkWriteDescriptorSet w[B_Count] {};
    uint32_t n = 0;
    auto push = [&](uint32_t binding, VkDescriptorType type)
    {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set;
        w[n].dstBinding = binding;
        w[n].descriptorCount = 1;
        w[n].descriptorType = type;
        return n++;
    };

    w[push(B_Constants, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)].pBufferInfo = &cb;
    w[push(B_SamplerPoint, VK_DESCRIPTOR_TYPE_SAMPLER)].pImageInfo = &pointInfo;
    w[push(B_SamplerLinear, VK_DESCRIPTOR_TYPE_SAMPLER)].pImageInfo = &linearInfo;
    for (uint32_t i = 0; i < 4; i++)
        w[push(B_Srv0 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)].pImageInfo = &srvs[i];
    for (uint32_t i = 0; i < 2; i++)
        w[push(B_Uav0 + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)].pImageInfo = &uavs[i];
    w[push(B_Uav2, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)].pTexelBufferView = &_motionCounterView;

    vkUpdateDescriptorSets(Device, n, w, 0, nullptr);
}

bool SGSR2FeatureVk::InitInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    if (IsInited())
        return true;

    LOG_INFO("Initializing SGSR2 (Vulkan) {}x{} -> {}x{}", RenderWidth(), RenderHeight(), DisplayWidth(),
             DisplayHeight());

    if (!CreatePipelines() || !CreateResources())
    {
        LOG_ERROR("SGSR2 Vulkan initialisation failed");
        return false;
    }

    // The Convert pass only ever adds to this buffer, so freshly allocated
    // device memory would be read back as whatever was there before. Nothing
    // reads it on this backend yet -- bSameCamera is still D3D12-only -- but
    // an uninitialised counter is not a state worth leaving lying around.
    vkCmdFillBuffer(InCmdBuffer, _motionCounter, 0, VK_WHOLE_SIZE, 0);

    VkBufferMemoryBarrier mcb { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    mcb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mcb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    mcb.srcQueueFamilyIndex = mcb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    mcb.buffer = _motionCounter;
    mcb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 1, &mcb, 0, nullptr);

    _historyValid = false;
    SetInit(true);
    return true;
}

bool SGSR2FeatureVk::EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    if (!IsInited())
        return false;

    NVSDK_NGX_Resource_VK* paramColor = nullptr;
    NVSDK_NGX_Resource_VK* paramVelocity = nullptr;
    NVSDK_NGX_Resource_VK* paramDepth = nullptr;
    NVSDK_NGX_Resource_VK* paramOutput = nullptr;
    NVSDK_NGX_Resource_VK* paramReactive = nullptr;

    InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);
    InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);
    InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);
    if (!Config::Instance()->DisableReactiveMask.value_or(true))
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactive);

    // Depth is optional here for the same reason as on D3D12: it only feeds the
    // disocclusion test, and some games never supply one.
    if (paramColor == nullptr || paramVelocity == nullptr || paramOutput == nullptr)
    {
        LOG_ERROR("Missing inputs: color {}, depth {}, mv {}, output {}", paramColor != nullptr, paramDepth != nullptr,
                  paramVelocity != nullptr, paramOutput != nullptr);
        return false;
    }

    if (paramDepth == nullptr && !_loggedNoDepth)
    {
        _loggedNoDepth = true;
        LOG_WARN("No depth buffer supplied; running without disocclusion detection. "
                 "Expect ghosting where geometry is revealed from behind an edge.");
    }

    _constants.hasDepth = (paramDepth != nullptr) ? 1u : 0u;
    if (paramDepth != nullptr)
    {
        _constants.depthSize[0] = paramDepth->Resource.ImageViewInfo.Width;
        _constants.depthSize[1] = paramDepth->Resource.ImageViewInfo.Height;
    }
    else
    {
        _constants.depthSize[0] = RenderWidth();
        _constants.depthSize[1] = RenderHeight();
    }

    _constants.reactiveStrength = 0.0f;
    if (paramReactive != nullptr)
    {
        const float bias = Config::Instance()->DlssReactiveMaskBias.value_or_default();
        _constants.reactiveStrength = bias > 0.0f ? bias : 0.35f;
    }
    if (!_loggedReactive)
    {
        _loggedReactive = true;
        LOG_INFO("Reactive mask: {} (strength {})",
                 paramReactive != nullptr
                     ? "present"
                     : (Config::Instance()->DisableReactiveMask.value_or(true) ? "disabled by config" : "not supplied"),
                 _constants.reactiveStrength);
    }

    if (!UpdateSharedConstants(InParameters))
        return false;

    std::memcpy(_constantMapped + _ringIndex * _constantStride, &_constants, sizeof(SGSR2Constants));
    return DispatchPasses(InCmdBuffer, paramColor, paramVelocity, paramDepth, paramOutput, paramReactive);
}

bool SGSR2FeatureVk::DispatchPasses(VkCommandBuffer cmd, NVSDK_NGX_Resource_VK* color,
                                    NVSDK_NGX_Resource_VK* velocity, NVSDK_NGX_Resource_VK* depth,
                                    NVSDK_NGX_Resource_VK* output, NVSDK_NGX_Resource_VK* reactive)
{
    const uint32_t rw = RenderWidth(), rh = RenderHeight();
    // TargetWidth, not DisplayWidth: UpdateSharedConstants writes displaySize
    // from TargetWidth, and the upscale pass bounds-checks against it. They are
    // equal until output scaling moves the target, and must not disagree then.
    const uint32_t dw = TargetWidth(), dh = TargetHeight();

    Image& prevHistory = _history[_historyIndex];
    Image& nextHistory = _history[_historyIndex ^ 1];

    // Game-supplied images are already in a shader-readable layout by contract;
    // ours are not, so only ours are transitioned.
    // Source access masks describe what the PREVIOUS frame did to each image.
    Barrier(cmd, _motionDepthClipAlpha, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT);
    Barrier(cmd, _ycocg, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    Barrier(cmd, prevHistory, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    Barrier(cmd, nextHistory, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    Barrier(cmd, _output, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    const VkDeviceSize cbOffset = _ringIndex * _constantStride;

    // The layout declared in a descriptor has to be the layout the image is
    // actually in. Ours are kept in GENERAL, because they are written as
    // storage images by one pass and sampled by the next; the game's arrive
    // shader-readable, which is what the NGX contract promises. Two helpers
    // rather than one, so the distinction cannot be lost at a call site.
    auto gameSampled = [](VkImageView v)
    { return VkDescriptorImageInfo { VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; };
    auto ownSampled = [](VkImageView v)
    { return VkDescriptorImageInfo { VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL }; };
    auto storage = [](VkImageView v)
    { return VkDescriptorImageInfo { VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL }; };

    // A slot the shader does not read still needs a live view behind it, so
    // unused SRVs point at one of our own images rather than nothing -- and
    // therefore use the GENERAL spelling.
    // Must be a float-format view: the slots it fills are Texture2D<float>
    // (depth) and Texture2D<float4> (reactive), and a view's numeric format has
    // to match the shader's sampled type even when the read is branched around.
    // _ycocg is R32_UINT and would be wrong on both.
    VkImageView filler = _motionDepthClipAlpha.view;

    // ------------------------------------------------------------- Pass 1
    {
        VkDescriptorImageInfo srvs[4] = {
            gameSampled(color->Resource.ImageViewInfo.ImageView),
            depth != nullptr ? gameSampled(depth->Resource.ImageViewInfo.ImageView) : ownSampled(filler),
            gameSampled(velocity->Resource.ImageViewInfo.ImageView),
            reactive != nullptr ? gameSampled(reactive->Resource.ImageViewInfo.ImageView) : ownSampled(filler),
        };
        VkDescriptorImageInfo uavs[2] = { storage(_motionDepthClipAlpha.view), storage(_ycocg.view) };

        VkDescriptorSet set = _descriptorSets[_ringIndex * PassCount + 0];
        WriteDescriptors(set, srvs, uavs, _constantBuffer, cbOffset);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _convertPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(cmd, DivRoundUp(rw, kTileSize), DivRoundUp(rh, kTileSize), 1);
    }

    // Pass 2 reads what pass 1 wrote.
    VkMemoryBarrier mb { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);

    // ------------------------------------------------------------- Pass 2
    {
        VkDescriptorImageInfo srvs[4] = {
            ownSampled(prevHistory.view),
            ownSampled(_motionDepthClipAlpha.view),
            ownSampled(_ycocg.view),
            reactive != nullptr ? gameSampled(reactive->Resource.ImageViewInfo.ImageView) : ownSampled(filler),
        };
        VkDescriptorImageInfo uavs[2] = { storage(_output.view), storage(nextHistory.view) };

        VkDescriptorSet set = _descriptorSets[_ringIndex * PassCount + 1];
        WriteDescriptors(set, srvs, uavs, _constantBuffer, cbOffset);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _upscalePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(cmd, DivRoundUp(dw, kTileSize), DivRoundUp(dh, kTileSize), 1);
    }

    // Copy our fixed-format target into whatever the game handed us. The D3D12
    // backend can often write the game's output directly; here the storage
    // image format is baked into the SPIR-V, so going through our own image
    // keeps that independent of the swapchain format.
    Barrier(cmd, _output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);

    VkImageMemoryBarrier ob { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    // UNDEFINED rather than a guess at the game's current layout: the blit
    // covers the whole image, so nothing is lost by discarding, and assuming
    // the wrong source layout would be undefined behaviour.
    ob.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ob.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ob.srcQueueFamilyIndex = ob.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ob.image = output->Resource.ImageViewInfo.Image;
    // The game's view may target a non-zero mip or layer; transitioning and
    // blitting mip 0 regardless would write somewhere it never looks.
    ob.subresourceRange = output->Resource.ImageViewInfo.SubresourceRange;
    ob.srcAccessMask = 0;
    ob.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &ob);

    // vkCmdBlitImage needs TRANSFER_DST on the destination. When a post-pass is
    // active this is our own intermediate, which IFeature_Vk creates with it.
    // With every post-pass off the destination is the game's own image, and NGX
    // only promises STORAGE. Nothing in the parameter surface reports usage, so
    // this is flagged rather than silently relied on; the real fix is a copy
    // compute shader, which is not worth writing blind.
    if (!_loggedBlitAssumption)
    {
        _loggedBlitAssumption = true;
        LOG_DEBUG("Blitting into the output image; assumes TRANSFER_DST usage when no post-pass is active");
    }

    VkImageBlit blit {};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, output->Resource.ImageViewInfo.SubresourceRange.baseMipLevel,
                            output->Resource.ImageViewInfo.SubresourceRange.baseArrayLayer, 1 };
    blit.srcOffsets[1] = { (int32_t) dw, (int32_t) dh, 1 };
    blit.dstOffsets[1] = { (int32_t) output->Resource.ImageViewInfo.Width,
                           (int32_t) output->Resource.ImageViewInfo.Height, 1 };
    vkCmdBlitImage(cmd, _output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   output->Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    // GENERAL, not SHADER_READ_ONLY_OPTIMAL. Whatever consumes this next -- RCAS,
    // output scaling, the magnifier, or the game itself -- expects GENERAL:
    // IFeature_Vk's post-passes each declare oldLayout=GENERAL for their input,
    // and NGX's ReadWrite contract is storage-image access. Handing them an
    // image in a different layout is a mismatched barrier, which on a tiler is
    // licensed to discard the contents.
    ob.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ob.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ob.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ob.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &ob);

    _historyIndex ^= 1;
    _historyValid = true;
    _ringIndex = (_ringIndex + 1) % FrameRingDepth;
    return true;
}

SGSR2FeatureVk::~SGSR2FeatureVk()
{
    if (Device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(Device);

    DestroyImage(_motionDepthClipAlpha);
    DestroyImage(_ycocg);
    DestroyImage(_history[0]);
    DestroyImage(_history[1]);
    DestroyImage(_output);

    if (_motionCounterView) vkDestroyBufferView(Device, _motionCounterView, nullptr);
    if (_motionCounter) vkDestroyBuffer(Device, _motionCounter, nullptr);
    if (_motionCounterMemory) vkFreeMemory(Device, _motionCounterMemory, nullptr);

    if (_constantMapped) vkUnmapMemory(Device, _constantMemory);
    if (_constantBuffer) vkDestroyBuffer(Device, _constantBuffer, nullptr);
    if (_constantMemory) vkFreeMemory(Device, _constantMemory, nullptr);

    if (_samplerPoint) vkDestroySampler(Device, _samplerPoint, nullptr);
    if (_samplerLinear) vkDestroySampler(Device, _samplerLinear, nullptr);

    if (_descriptorPool) vkDestroyDescriptorPool(Device, _descriptorPool, nullptr);
    if (_convertPipeline) vkDestroyPipeline(Device, _convertPipeline, nullptr);
    if (_upscalePipeline) vkDestroyPipeline(Device, _upscalePipeline, nullptr);
    if (_pipelineLayout) vkDestroyPipelineLayout(Device, _pipelineLayout, nullptr);
    if (_setLayout) vkDestroyDescriptorSetLayout(Device, _setLayout, nullptr);
}
