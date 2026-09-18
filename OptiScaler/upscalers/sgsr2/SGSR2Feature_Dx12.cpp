#include "pch.h"
#include "SGSR2Feature_Dx12.h"
#include "SGSR2_Shaders.h"

#include <Config.h>
#include <Util.h>
#include <shaders/Shader_Common.h>
#include <MathUtils.h>

#include <d3dcompiler.h>

// Render/display resolution is dispatched in 8x8 tiles, matching [numthreads].
static constexpr uint32_t kTileSize = 8;

static inline uint32_t DivRoundUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// SGSR2 works on a typed view of whatever the game handed us. Typeless and
// sRGB formats have to be resolved to something a compute SRV/UAV accepts.
static DXGI_FORMAT ResolveFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
        return format;
    }
}

static ID3D12Resource* CreateTexture(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                                     const wchar_t* name)
{
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ID3D12Resource* resource = nullptr;
    auto hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                              IID_PPV_ARGS(&resource));
    if (FAILED(hr))
    {
        LOG_ERROR("CreateCommittedResource failed for {0}: {1:X}", wstring_to_string(name), (UINT) hr);
        return nullptr;
    }

    resource->SetName(name);
    return resource;
}

bool SGSR2FeatureDx12::CreatePipelines(ID3D12Device* device)
{
    // Both passes bind b0 / t0..t2 / u0..u1, so one root signature serves both.
    D3D12_DESCRIPTOR_RANGE ranges[2] {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = SRV_Count;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = UAV_Count;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static samplers keep a sampler heap out of the picture entirely.
    D3D12_STATIC_SAMPLER_DESC samplers[2] {};
    for (int i = 0; i < 2; i++)
    {
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;  // s0, also used by Gather
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // s1

    D3D12_ROOT_SIGNATURE_DESC rsDesc {};
    rsDesc.NumParameters = _countof(params);
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = _countof(samplers);
    rsDesc.pStaticSamplers = samplers;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* serialized = nullptr;
    ID3DBlob* errors = nullptr;
    auto hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr))
    {
        LOG_ERROR("D3D12SerializeRootSignature failed: {0:X} {1}", (UINT) hr,
                  errors ? (const char*) errors->GetBufferPointer() : "");
        if (errors)
            errors->Release();
        return false;
    }

    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                     IID_PPV_ARGS(&_rootSignature));
    serialized->Release();
    if (errors)
        errors->Release();

    if (FAILED(hr))
    {
        LOG_ERROR("CreateRootSignature failed: {0:X}", (UINT) hr);
        return false;
    }

    _rootSignature->SetName(L"SGSR2_RootSignature");

    struct
    {
        const char* source;
        const char* name;
        ID3D12PipelineState** target;
    } passes[] = {
        { SGSR2_ConvertShader, "SGSR2_Convert", &_convertPipeline },
        { SGSR2_UpscaleShader, "SGSR2_Upscale", &_upscalePipeline },
    };

    for (auto& pass : passes)
    {
        // cs_5_0 keeps this compatible with Wine's d3dcompiler and vkd3d-proton,
        // which is what actually runs under Proton on ARM64.
        ID3DBlob* blob = CompileShader(pass.source, "CSMain", "cs_5_0");
        if (blob == nullptr)
        {
            LOG_ERROR("Failed to compile {0}", pass.name);
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc {};
        psoDesc.pRootSignature = _rootSignature;
        psoDesc.CS.pShaderBytecode = blob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = blob->GetBufferSize();

        hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pass.target));
        blob->Release();

        if (FAILED(hr))
        {
            LOG_ERROR("CreateComputePipelineState failed for {0}: {1:X}", pass.name, (UINT) hr);
            return false;
        }
    }

    _convertPipeline->SetName(L"SGSR2_ConvertPSO");
    _upscalePipeline->SetName(L"SGSR2_UpscalePSO");

    return true;
}

bool SGSR2FeatureDx12::CreateResources(ID3D12Device* device)
{
    const uint32_t rw = RenderWidth();
    const uint32_t rh = RenderHeight();
    const uint32_t dw = TargetWidth();
    const uint32_t dh = TargetHeight();

    _motionDepthClipAlpha = CreateTexture(device, rw, rh, DXGI_FORMAT_R16G16B16A16_FLOAT, L"SGSR2_MotionDepthClip");
    _ycocgColor = CreateTexture(device, rw, rh, DXGI_FORMAT_R32_UINT, L"SGSR2_YCoCg");
    _history[0] = CreateTexture(device, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, L"SGSR2_History0");
    _history[1] = CreateTexture(device, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, L"SGSR2_History1");
    _outputBuffer = CreateTexture(device, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, L"SGSR2_Output");

    if (_motionDepthClipAlpha == nullptr || _ycocgColor == nullptr || _history[0] == nullptr ||
        _history[1] == nullptr || _outputBuffer == nullptr)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = DescriptorsPerFrame * FrameRingDepth;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_descriptorHeap))))
    {
        LOG_ERROR("CreateDescriptorHeap failed");
        return false;
    }

    _descriptorHeap->SetName(L"SGSR2_DescriptorHeap");
    _descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Constant buffer: one 256-byte-aligned slot per frame in the ring, kept
    // mapped since it is rewritten every frame.
    const uint32_t cbStride = (sizeof(SGSR2Constants) + 255) & ~255u;

    D3D12_HEAP_PROPERTIES cbHeap {};
    cbHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = (UINT64) cbStride * FrameRingDepth;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&cbHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&_constantBuffer))))
    {
        LOG_ERROR("Constant buffer creation failed");
        return false;
    }

    _constantBuffer->SetName(L"SGSR2_Constants");

    D3D12_RANGE noRead { 0, 0 };
    if (FAILED(_constantBuffer->Map(0, &noRead, (void**) &_constantBufferMapped)))
    {
        LOG_ERROR("Constant buffer map failed");
        return false;
    }

    _historyValid = false;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE SGSR2FeatureDx12::BindPass(ID3D12Device* device, uint32_t passIndex,
                                                       ID3D12Resource* const* srvs, const DXGI_FORMAT* srvFormats,
                                                       ID3D12Resource* const* uavs, const DXGI_FORMAT* uavFormats)
{
    const uint32_t base = (_ringIndex * DescriptorsPerFrame) + (passIndex * DescriptorsPerPass);

    auto cpu = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = _descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T) base * _descriptorSize;
    gpu.ptr += (UINT64) base * _descriptorSize;

    auto cursor = cpu;

    for (uint32_t i = 0; i < SRV_Count; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc {};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        desc.Format = srvFormats[i];
        device->CreateShaderResourceView(srvs[i], &desc, cursor);
        cursor.ptr += _descriptorSize;
    }

    for (uint32_t i = 0; i < UAV_Count; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Format = uavFormats[i];
        device->CreateUnorderedAccessView(uavs[i], nullptr, &desc, cursor);
        cursor.ptr += _descriptorSize;
    }

    return gpu;
}

bool SGSR2FeatureDx12::UpdateConstants(NVSDK_NGX_Parameter* InParameters)
{
    const float rw = (float) RenderWidth();
    const float rh = (float) RenderHeight();
    const float dw = (float) TargetWidth();
    const float dh = (float) TargetHeight();

    _constants.renderSize[0] = RenderWidth();
    _constants.renderSize[1] = RenderHeight();
    _constants.displaySize[0] = TargetWidth();
    _constants.displaySize[1] = TargetHeight();

    _constants.renderSizeRcp[0] = 1.0f / rw;
    _constants.renderSizeRcp[1] = 1.0f / rh;
    _constants.displaySizeRcp[0] = 1.0f / dw;
    _constants.displaySizeRcp[1] = 1.0f / dh;

    float jitterX = 0.0f, jitterY = 0.0f;
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);
    _constants.jitterOffset[0] = jitterX;
    _constants.jitterOffset[1] = jitterY;

    // NGX motion vectors are normally in render pixels; MV_Scale converts the
    // texture's units into that space. Fold in the pixels -> clip-space factor
    // (2 / size) and the D3D12 Y sign so the shader can just multiply.
    float mvScaleX = 1.0f, mvScaleY = 1.0f;
    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX) != NVSDK_NGX_Result_Success ||
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY) != NVSDK_NGX_Result_Success)
    {
        mvScaleX = 1.0f;
        mvScaleY = 1.0f;
    }

    _constants.motionVectorScale[0] = -2.0f * mvScaleX / rw;
    _constants.motionVectorScale[1] = 2.0f * mvScaleY / rh;

    // Unused while motion comes from the MV texture, but keep it a well-formed
    // identity so the shader's fallback path can never produce garbage.
    memset(_constants.clipToPrevClip, 0, sizeof(_constants.clipToPrevClip));
    _constants.clipToPrevClip[0] = 1.0f;
    _constants.clipToPrevClip[5] = 1.0f;
    _constants.clipToPrevClip[10] = 1.0f;
    _constants.clipToPrevClip[15] = 1.0f;

    // SGSR2 divides by preExposure, so it must never be zero.
    float exposure = 1.0f;
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &exposure) != NVSDK_NGX_Result_Success ||
        exposure == 0.0f)
        exposure = 1.0f;
    _constants.preExposure = exposure;

    // DLSS carries neither FOV nor the near plane, so OptiScaler exposes its own
    // keys for games that can supply them and falls back to the same user config
    // the FSR backends use.
    float verticalFov = 0.0f;
    if (InParameters->Get(OptiKeys::FSR_CameraFovVertical, &verticalFov) != NVSDK_NGX_Result_Success ||
        verticalFov <= 0.0f)
        verticalFov = OptiMath::GetRadiansFromDeg(Config::Instance()->FsrVerticalFov.value_or_default());

    // SGSR2 wants tan(hfov/2), used only to scale its depth-separation heuristic.
    _constants.cameraFovAngleHor = tanf(verticalFov / 2.0f) * rw / rh;

    float cameraNear = 0.0f;
    if (InParameters->Get(OptiKeys::FSR_NearPlane, &cameraNear) != NVSDK_NGX_Result_Success || cameraNear <= 0.0f)
        cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
    _constants.cameraNear = cameraNear;

    _constants.minLerpContribution = 0.3f;

    // Nine-tap neighbourhood only pays off when the camera is still; without a
    // previous view-projection the best available proxy is the reset flag.
    _constants.bSameCamera = _sameCamera ? 1u : 0u;

    int reset = 0;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    _constants.reset = (reset != 0 || !_historyValid) ? 1u : 0u;

    const uint32_t cbStride = (sizeof(SGSR2Constants) + 255) & ~255u;
    memcpy(_constantBufferMapped + (size_t) _ringIndex * cbStride, &_constants, sizeof(SGSR2Constants));

    return true;
}

bool SGSR2FeatureDx12::InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (IsInited())
        return true;

    // IFeature_Dx12::Init has already set Device from the caller; do not re-fetch
    // it from State, whose currentD3D12Device is cleared when a device is released.
    if (Device == nullptr)
    {
        LOG_ERROR("No D3D12 device");
        return false;
    }

    // Target size is the backend's responsibility -- it is still zero here.
    // Output scaling renders at a multiple of display size and downsamples later.
    if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
        (LowResMV() || RenderWidth() == DisplayWidth()))
    {
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or_default();

        if (ssMulti < 0.5f)
        {
            ssMulti = 0.5f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }
        else if (ssMulti > 3.0f)
        {
            ssMulti = 3.0f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }

        _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
        _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
    }
    else
    {
        _targetWidth = DisplayWidth();
        _targetHeight = DisplayHeight();
    }

    if (RenderWidth() == 0 || RenderHeight() == 0 || TargetWidth() == 0 || TargetHeight() == 0)
    {
        LOG_ERROR("Invalid dimensions {0}x{1} -> {2}x{3}", RenderWidth(), RenderHeight(), TargetWidth(),
                  TargetHeight());
        return false;
    }

    LOG_INFO("Initializing SGSR2 {0}x{1} -> {2}x{3}", RenderWidth(), RenderHeight(), TargetWidth(), TargetHeight());

    if (!CreatePipelines(Device))
    {
        LOG_ERROR("Pipeline creation failed");
        return false;
    }

    if (!CreateResources(Device))
    {
        LOG_ERROR("Resource creation failed");
        ReleaseResources();
        return false;
    }

    SetInit(true);
    return true;
}

bool SGSR2FeatureDx12::EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (!IsInited() || InCommandList == nullptr)
        return false;

    ID3D12Resource* paramColor = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    ID3D12Resource* paramDepth = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    ID3D12Resource* paramVelocity = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

    ID3D12Resource* paramOutput = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramColor == nullptr || paramDepth == nullptr || paramVelocity == nullptr || paramOutput == nullptr)
    {
        LOG_ERROR("Missing inputs: color {0}, depth {1}, mv {2}, output {3}", paramColor != nullptr,
                  paramDepth != nullptr, paramVelocity != nullptr, paramOutput != nullptr);
        return false;
    }

    _hasColor = true;
    _hasDepth = true;
    _hasMV = true;
    _hasOutput = true;

    UpdateConstants(InParameters);

    // The game's resources arrive in whatever state it left them in; OptiScaler
    // records that per-resource so it can be restored afterwards.
    auto colorState = Config::Instance()->ColorResourceBarrier.value_or(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    auto depthState = Config::Instance()->DepthResourceBarrier.value_or(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto mvState = Config::Instance()->MVResourceBarrier.value_or(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    auto outputState = Config::Instance()->OutputResourceBarrier.value_or(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ResourceBarrier(InCommandList, paramColor, (D3D12_RESOURCE_STATES) colorState,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(InCommandList, paramDepth, (D3D12_RESOURCE_STATES) depthState,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(InCommandList, paramVelocity, (D3D12_RESOURCE_STATES) mvState,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12Resource* prevHistory = _history[_historyIndex];
    ID3D12Resource* nextHistory = _history[_historyIndex ^ 1];

    ID3D12DescriptorHeap* heaps[] = { _descriptorHeap };
    InCommandList->SetDescriptorHeaps(1, heaps);
    InCommandList->SetComputeRootSignature(_rootSignature);

    const uint32_t cbStride = (sizeof(SGSR2Constants) + 255) & ~255u;
    InCommandList->SetComputeRootConstantBufferView(
        0, _constantBuffer->GetGPUVirtualAddress() + (UINT64) _ringIndex * cbStride);

    // ---------------------------------------------------------------- Pass 1
    {
        ID3D12Resource* srvs[SRV_Count] = { paramColor, paramDepth, paramVelocity };
        DXGI_FORMAT srvFormats[SRV_Count] = { ResolveFormat(paramColor->GetDesc().Format),
                                              ResolveFormat(paramDepth->GetDesc().Format),
                                              ResolveFormat(paramVelocity->GetDesc().Format) };

        ID3D12Resource* uavs[UAV_Count] = { _motionDepthClipAlpha, _ycocgColor };
        DXGI_FORMAT uavFormats[UAV_Count] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_UINT };

        auto table = BindPass(Device, 0, srvs, srvFormats, uavs, uavFormats);

        InCommandList->SetPipelineState(_convertPipeline);
        InCommandList->SetComputeRootDescriptorTable(1, table);

        auto uavTable = table;
        uavTable.ptr += (UINT64) SRV_Count * _descriptorSize;
        InCommandList->SetComputeRootDescriptorTable(2, uavTable);

        InCommandList->Dispatch(DivRoundUp(RenderWidth(), kTileSize), DivRoundUp(RenderHeight(), kTileSize), 1);
    }

    // Convert's UAV writes are Upscale's SRV reads.
    {
        D3D12_RESOURCE_BARRIER barriers[2] {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = _motionDepthClipAlpha;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1] = barriers[0];
        barriers[1].Transition.pResource = _ycocgColor;

        InCommandList->ResourceBarrier(2, barriers);
    }

    // Previous history is read as an SRV this frame.
    {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = prevHistory;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        InCommandList->ResourceBarrier(1, &barrier);
    }

    // ---------------------------------------------------------------- Pass 2
    {
        ID3D12Resource* srvs[SRV_Count] = { prevHistory, _motionDepthClipAlpha, _ycocgColor };
        DXGI_FORMAT srvFormats[SRV_Count] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                              DXGI_FORMAT_R32_UINT };

        ID3D12Resource* uavs[UAV_Count] = { _outputBuffer, nextHistory };
        DXGI_FORMAT uavFormats[UAV_Count] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT };

        auto table = BindPass(Device, 1, srvs, srvFormats, uavs, uavFormats);

        InCommandList->SetPipelineState(_upscalePipeline);
        InCommandList->SetComputeRootDescriptorTable(1, table);

        auto uavTable = table;
        uavTable.ptr += (UINT64) SRV_Count * _descriptorSize;
        InCommandList->SetComputeRootDescriptorTable(2, uavTable);

        InCommandList->Dispatch(DivRoundUp(TargetWidth(), kTileSize), DivRoundUp(TargetHeight(), kTileSize), 1);
    }

    // Hand the result to the rest of OptiScaler's pipeline via the game's output.
    {
        D3D12_RESOURCE_BARRIER barriers[2] {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = _outputBuffer;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = paramOutput;
        barriers[1].Transition.StateBefore = (D3D12_RESOURCE_STATES) outputState;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        InCommandList->ResourceBarrier(2, barriers);
    }

    InCommandList->CopyResource(paramOutput, _outputBuffer);

    // Restore everything to the states the caller expects.
    {
        D3D12_RESOURCE_BARRIER barriers[5] {};
        auto make = [](ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER b {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = res;
            b.Transition.StateBefore = before;
            b.Transition.StateAfter = after;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            return b;
        };

        barriers[0] = make(_outputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[1] = make(paramOutput, D3D12_RESOURCE_STATE_COPY_DEST, (D3D12_RESOURCE_STATES) outputState);
        barriers[2] = make(_motionDepthClipAlpha, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[3] = make(_ycocgColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[4] = make(prevHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        InCommandList->ResourceBarrier(5, barriers);
    }

    ResourceBarrier(InCommandList, paramColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    (D3D12_RESOURCE_STATES) colorState);
    ResourceBarrier(InCommandList, paramDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    (D3D12_RESOURCE_STATES) depthState);
    ResourceBarrier(InCommandList, paramVelocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    (D3D12_RESOURCE_STATES) mvState);

    _historyIndex ^= 1;
    _historyValid = true;
    _ringIndex = (_ringIndex + 1) % FrameRingDepth;
    _frameCount++;

    return true;
}

void SGSR2FeatureDx12::ReleaseResources()
{
    auto release = [](auto*& p)
    {
        if (p != nullptr)
        {
            p->Release();
            p = nullptr;
        }
    };

    if (_constantBuffer != nullptr && _constantBufferMapped != nullptr)
    {
        _constantBuffer->Unmap(0, nullptr);
        _constantBufferMapped = nullptr;
    }

    release(_constantBuffer);
    release(_motionDepthClipAlpha);
    release(_ycocgColor);
    release(_history[0]);
    release(_history[1]);
    release(_outputBuffer);
    release(_descriptorHeap);
    release(_convertPipeline);
    release(_upscalePipeline);
    release(_rootSignature);
}

SGSR2FeatureDx12::~SGSR2FeatureDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    ReleaseResources();
}
