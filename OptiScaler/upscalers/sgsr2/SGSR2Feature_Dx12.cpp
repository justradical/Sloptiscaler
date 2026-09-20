#include "pch.h"
#include "SGSR2Feature_Dx12.h"
#include "SGSR2_Shaders.h"

#include <Config.h>
#include <Util.h>
#include <shaders/Shader_Common.h>
#include <MathUtils.h>

#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>

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

    const char* timingEnv = std::getenv("OPTI_SGSR2_TIMING");
    _timingEnabled = timingEnv != nullptr && timingEnv[0] == '1';

    if (_timingEnabled)
    {
        auto* queue = State::Instance().currentCommandQueue;
        if (queue == nullptr || FAILED(queue->GetTimestampFrequency(&_timerFrequency)) || _timerFrequency == 0)
        {
            LOG_WARN("OPTI_SGSR2_TIMING set but no queue timestamp frequency; timing disabled");
            _timingEnabled = false;
        }
    }

    if (_timingEnabled)
    {
        D3D12_QUERY_HEAP_DESC qh {};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = TimestampsPerFrame * FrameRingDepth;

        D3D12_HEAP_PROPERTIES rbHeap {};
        rbHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC rbDesc {};
        rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rbDesc.Width = (UINT64) qh.Count * sizeof(uint64_t);
        rbDesc.Height = 1;
        rbDesc.DepthOrArraySize = 1;
        rbDesc.MipLevels = 1;
        rbDesc.Format = DXGI_FORMAT_UNKNOWN;
        rbDesc.SampleDesc.Count = 1;
        rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&_timestampHeap))) ||
            FAILED(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&_timestampReadback))))
        {
            LOG_WARN("Timestamp query resources failed; timing disabled");
            _timingEnabled = false;
        }
        else
        {
            LOG_INFO("GPU timing enabled, timestamp frequency {0} Hz", _timerFrequency);
        }
    }

    // Static-camera detection resources. The counter is cleared each frame,
    // added to by the Convert pass, and copied into a ring slot so the value can
    // be read back once the frame that wrote it has retired.
    {
        D3D12_HEAP_PROPERTIES defHeap {};
        defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC cd {};
        cd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cd.Width = sizeof(uint32_t);
        cd.Height = 1;
        cd.DepthOrArraySize = 1;
        cd.MipLevels = 1;
        cd.Format = DXGI_FORMAT_UNKNOWN;
        cd.SampleDesc.Count = 1;
        cd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        cd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES rbHeap {};
        rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = cd;
        rd.Width = sizeof(uint32_t) * FrameRingDepth;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_DESCRIPTOR_HEAP_DESC ch {};
        ch.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ch.NumDescriptors = 1;
        ch.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // ClearUAV needs a non-shader-visible handle

        if (FAILED(device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &cd,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&_motionCounter))) ||
            FAILED(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &rd,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&_motionCounterReadback))) ||
            FAILED(device->CreateDescriptorHeap(&ch, IID_PPV_ARGS(&_clearHeap))))
        {
            LOG_WARN("Static-camera detection unavailable; the nine-tap path stays off");
            if (_motionCounter) { _motionCounter->Release(); _motionCounter = nullptr; }
            if (_motionCounterReadback) { _motionCounterReadback->Release(); _motionCounterReadback = nullptr; }
            if (_clearHeap) { _clearHeap->Release(); _clearHeap = nullptr; }
        }
        else
        {
            _motionCounter->SetName(L"SGSR2_MotionCounter");
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud {};
            ud.Format = DXGI_FORMAT_R32_UINT;
            ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = 1;
            device->CreateUnorderedAccessView(_motionCounter, nullptr, &ud,
                                              _clearHeap->GetCPUDescriptorHandleForHeapStart());
        }
    }

    _historyValid = false;
    return true;
}

// Decide whether the camera is holding still, from how many sampled pixels the
// Convert pass found in motion. Read a ring slot older than the frames in
// flight, so nothing has to be waited on.
void SGSR2FeatureDx12::UpdateSameCamera(ID3D12GraphicsCommandList* InCommandList)
{
    if (_motionCounter == nullptr)
        return;

    const uint32_t slot = _ringIndex;

    if (_frameCount >= FrameRingDepth)
    {
        void* mapped = nullptr;
        D3D12_RANGE r { (SIZE_T) slot * sizeof(uint32_t), (SIZE_T) (slot + 1) * sizeof(uint32_t) };
        if (SUCCEEDED(_motionCounterReadback->Map(0, &r, &mapped)) && mapped != nullptr)
        {
            uint32_t moving = 0;
            memcpy(&moving, (const uint8_t*) mapped + r.Begin, sizeof(moving));
            D3D12_RANGE noWrite { 0, 0 };
            _motionCounterReadback->Unmap(0, &noWrite);

            // Sampled points are one per 8x8 block.
            const uint32_t sampled = DivRoundUp(RenderWidth(), 8) * DivRoundUp(RenderHeight(), 8);
            const float fraction = sampled > 0 ? (float) moving / (float) sampled : 1.0f;

            // A little movement is always present -- animated props, foliage,
            // a character idling -- so the test is for a mostly-still frame
            // rather than a perfectly still one.
            if (fraction < 0.02f)
                _sameCameraFrames++;
            else
                _sameCameraFrames = 0;

            _lastMovingFraction = fraction;
            _maxMovingFraction = std::max(_maxMovingFraction, fraction);
        }
    }

    _sameCamera = _sameCameraFrames > 1; // one settled frame before widening

    // Clear for this frame, then hand the value to the readback ring.
    if (_clearHeap != nullptr)
    {
        auto gpu = _descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += (UINT64) ((_ringIndex * DescriptorsPerFrame) + SRV_Count + 2) * _descriptorSize;
        const UINT zero[4] = { 0, 0, 0, 0 };
        InCommandList->ClearUnorderedAccessViewUint(gpu, _clearHeap->GetCPUDescriptorHandleForHeapStart(),
                                                    _motionCounter, zero, 0, nullptr);
    }
}

void SGSR2FeatureDx12::ResolveTimestamps(ID3D12GraphicsCommandList* InCommandList)
{
    const uint32_t base = _ringIndex * TimestampsPerFrame;

    auto now = std::chrono::steady_clock::now();
    if (_timingLastEvaluate.time_since_epoch().count() != 0)
    {
        _timingWallMs += std::chrono::duration<double, std::milli>(now - _timingLastEvaluate).count();
        _timingWallSamples++;
    }
    _timingLastEvaluate = now;

    uint64_t ts[TimestampsPerFrame] {};
    D3D12_RANGE readRange { (SIZE_T) base * sizeof(uint64_t), (SIZE_T) (base + TimestampsPerFrame) * sizeof(uint64_t) };
    void* mapped = nullptr;

    if (SUCCEEDED(_timestampReadback->Map(0, &readRange, &mapped)) && mapped != nullptr)
    {
        memcpy(ts, (const uint8_t*) mapped + readRange.Begin, sizeof(ts));
        D3D12_RANGE noWrite { 0, 0 };
        _timestampReadback->Unmap(0, &noWrite);
    }

    if (ts[1] > ts[0] && ts[3] > ts[2])
    {
        const double toMs = 1000.0 / (double) _timerFrequency;
        const double p1 = (double) (ts[1] - ts[0]) * toMs;
        const double p2 = (double) (ts[3] - ts[2]) * toMs;

        _timingPass1Ms += p1;
        _timingPass2Ms += p2;
        _timingWorstMs = std::max(_timingWorstMs, p1 + p2);
        _timingSamples++;

        if (_timingSamples >= 300)
        {
            const double n = (double) _timingSamples;
            const double wall = _timingWallSamples > 0 ? _timingWallMs / (double) _timingWallSamples : 0.0;
            const double cost = (_timingPass1Ms + _timingPass2Ms) / n;

            LOG_INFO("SGSR2 {0}x{1} -> {2}x{3} over {4} frames: convert {5:.3f} + upscale {6:.3f} = {7:.3f} ms/frame "
                     "GPU (worst {8:.3f}); frame {9:.2f} ms = {10:.1f} fps, upscaler is {11:.1f}% of it"
                     " | staticCamera={12} ({13} settled, moving now {14:.3f} peak {15:.3f})",
                     RenderWidth(), RenderHeight(), TargetWidth(), TargetHeight(), _timingSamples,
                     _timingPass1Ms / n, _timingPass2Ms / n, cost, _timingWorstMs, wall,
                     wall > 0.0 ? 1000.0 / wall : 0.0, wall > 0.0 ? 100.0 * cost / wall : 0.0, _sameCamera,
                     _sameCameraFrames, _lastMovingFraction, _maxMovingFraction);
            _maxMovingFraction = 0.0f;

            _timingPass1Ms = 0.0;
            _timingPass2Ms = 0.0;
            _timingWorstMs = 0.0;
            _timingSamples = 0;
            _timingWallMs = 0.0;
            _timingWallSamples = 0;
        }
    }

    InCommandList->ResolveQueryData(_timestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, base, TimestampsPerFrame,
                                    _timestampReadback, (UINT64) base * sizeof(uint64_t));
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
        desc.Format = uavFormats[i];

        // The motion counter is a buffer; everything else here is a texture.
        if (uavs[i] != nullptr && uavs[i]->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = 1;
        }
        else
        {
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        }

        if (uavs[i] != nullptr)
            device->CreateUnorderedAccessView(uavs[i], nullptr, &desc, cursor);
        cursor.ptr += _descriptorSize;
    }

    return gpu;
}


// Copy the game's velocity texture into system memory and report what is in it.
// SGSR2 reading zero motion and the game supplying an empty buffer look
// identical from inside a shader, and that ambiguity cost a lot of time: the
// motion-field debug view showed 99.5% of pixels at exactly zero in Hi-Fi Rush
// and 100% in Baldur's Gate 3, which reads as "no game gives us motion" when it
// actually means "we cannot read what we were given".
//
// The counts are split by the NGX subrect because the declared valid region and
// the rest of an oversized allocation can differ.
void SGSR2FeatureDx12::DumpVelocity(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* velocity)
{
    auto desc = velocity->GetDesc();

    if (_mvReadbackState == 0)
    {
        if (_frameCount < 90) // let the game settle into rendering
            return;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
        UINT64 bytes = 0;
        Device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);

        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rb {};
        rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rb.Width = bytes;
        rb.Height = 1;
        rb.DepthOrArraySize = 1;
        rb.MipLevels = 1;
        rb.Format = DXGI_FORMAT_UNKNOWN;
        rb.SampleDesc.Count = 1;
        rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&_mvReadback))))
        {
            LOG_WARN("velocity readback: allocation failed");
            _mvReadbackState = 2;
            return;
        }

        _mvReadbackPitch = fp.Footprint.RowPitch;
        _mvReadbackW = fp.Footprint.Width;
        _mvReadbackH = fp.Footprint.Height;
        _mvReadbackFormat = desc.Format;

        D3D12_TEXTURE_COPY_LOCATION src {};
        src.pResource = velocity;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst {};
        dst.pResource = _mvReadback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;

        ResourceBarrier(InCommandList, velocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_SOURCE);
        InCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        ResourceBarrier(InCommandList, velocity, D3D12_RESOURCE_STATE_COPY_SOURCE,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        _mvReadbackState = 1;
        _mvReadbackFrame = _frameCount;
        return;
    }

    if (_mvReadbackState != 1 || _frameCount < _mvReadbackFrame + FrameRingDepth + 2)
        return;

    _mvReadbackState = 2;

    void* mapped = nullptr;
    if (FAILED(_mvReadback->Map(0, nullptr, &mapped)) || mapped == nullptr)
    {
        LOG_WARN("velocity readback: map failed");
        return;
    }

    const bool isHalf = (_mvReadbackFormat == DXGI_FORMAT_R16G16_FLOAT);
    const bool isFloat = (_mvReadbackFormat == DXGI_FORMAT_R32G32_FLOAT);
    const uint32_t texel = isHalf ? 4 : (isFloat ? 8 : 0);

    auto halfToFloat = [](uint16_t v)
    {
        uint32_t sign = (v >> 15) & 1, exp = (v >> 10) & 0x1F, man = v & 0x3FF;
        if (exp == 0)
            return 0.0f;
        uint32_t f = (sign << 31) | ((exp - 15 + 127) << 23) | (man << 13);
        float out;
        memcpy(&out, &f, 4);
        return out;
    };

    const uint32_t rw = RenderWidth(), rh = RenderHeight();
    uint64_t nz = 0, tot = 0, cnz = 0, ctot = 0;
    float lo = 1e30f, hi = -1e30f;

    for (uint32_t y = 0; texel && y < _mvReadbackH; y += 4)
    {
        const uint8_t* row = (const uint8_t*) mapped + (uint64_t) y * _mvReadbackPitch;
        for (uint32_t x = 0; x < _mvReadbackW; x += 4)
        {
            float vx, vy;
            if (isFloat)
            {
                const float* p = (const float*) (row + (uint64_t) x * texel);
                vx = p[0];
                vy = p[1];
            }
            else
            {
                const uint16_t* h = (const uint16_t*) (row + (uint64_t) x * texel);
                vx = halfToFloat(h[0]);
                vy = halfToFloat(h[1]);
            }

            const bool corner = (x < rw && y < rh);
            tot++;
            if (corner)
                ctot++;
            if (vx != 0.0f || vy != 0.0f)
            {
                nz++;
                lo = std::min(lo, std::min(vx, vy));
                hi = std::max(hi, std::max(vx, vy));
                if (corner)
                    cnz++;
            }
        }
    }

    D3D12_RANGE noWrite { 0, 0 };
    _mvReadback->Unmap(0, &noWrite);

    LOG_INFO("velocity readback: {0}x{1} fmt {2} -- whole {3}/{4} non-zero, NGX subrect {5}x{6} {7}/{8} non-zero, "
             "range [{9}, {10}]",
             _mvReadbackW, _mvReadbackH, (int) _mvReadbackFormat, nz, tot, rw, rh, cnz, ctot, lo, hi);
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

    // SGSR2 consumes "Motion" in clip-space (NDC) units, reprojecting with
    //     PrevUV = (Hruv.x - 0.5*Motion.x, Hruv.y + 0.5*Motion.y)
    // and NGX supplies vectors in render pixels, so with prevPixel = cur + mv:
    //     Motion.x = -2 * mv.x / renderWidth
    //     Motion.y = +2 * mv.y / renderHeight
    // The X sign is D3D12 clip-space Y up against texture V down.
    //
    // The units are measured, not assumed: with the fetch coordinate fixed, a
    // classifier over the raw values puts 9.4% of pixels above 1.0 and none in
    // the 1e-3..1 band, i.e. pixel magnitudes. Every earlier attempt to pin
    // this down was worthless because the fetch was returning zero.
    //
    // The Y sign is measured too. Walking forward makes the camera follow, so
    // screen flow is radial and Y is exercised without needing a vertical
    // camera move. Same walk, same save, detail retained in motion relative to
    // a still frame: +2/h keeps 1.05, -2/h keeps 0.56.
    _constants.motionVectorScale[0] = -2.0f * mvScaleX / rw;
    _constants.motionVectorScale[1] = 2.0f * mvScaleY / rh;

    // Temporary tuning hook: lets the motion-vector convention be dialled in on
    // a device without a rebuild. OPTI_SGSR2_MVX/MVY override the scale outright.
    if (const char* e = std::getenv("OPTI_SGSR2_MVX"))
        _constants.motionVectorScale[0] = (float) atof(e);
    if (const char* e = std::getenv("OPTI_SGSR2_MVY"))
        _constants.motionVectorScale[1] = (float) atof(e);

    // The units NGX motion vectors arrive in vary by engine, and this scale is
    // the single place that convention lives. Report it once so a smeared or
    // doubled image can be diagnosed without guessing.
    if (_frameCount == 0)
        LOG_INFO("MV convention: NGX MV_Scale=({0}, {1}) render={2}x{3} -> motionVectorScale=({4}, {5})", mvScaleX,
                 mvScaleY, RenderWidth(), RenderHeight(), _constants.motionVectorScale[0],
                 _constants.motionVectorScale[1]);

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

    // Logged here rather than with the MV line above: preExposure is only
    // settled on the line before this, so reporting it earlier printed the
    // previous frame's value -- zero on the first frame, which reads as a
    // black-output bug that is not there.
    if (_frameCount == 0)
        LOG_INFO("  flags: DepthInverted={0} JitteredMV={1} LowResMV={2} AutoExposure={3} preExposure={4}",
                 DepthInverted(), JitteredMV(), LowResMV(), AutoExposure(), _constants.preExposure);

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

    // The nine-tap neighbourhood only pays off when the camera is still. There
    // is no previous view-projection to compare against, so this comes from the
    // motion counter the Convert pass fills -- see UpdateSameCamera.
    _constants.bSameCamera = _sameCamera ? 1u : 0u;

    // Reverse-Z flips which end of the range is "near", so the Convert pass has
    // to dilate depth with max() instead of min() and invert its far-plane test.
    _constants.depthInverted = DepthInverted() ? 1u : 0u;

    // Diagnostics: OPTI_SGSR2_DEBUG=1 swaps the output for a motion-field view.
    const char* dbgEnv = std::getenv("OPTI_SGSR2_DEBUG");
    _constants.debugMode = (dbgEnv != nullptr) ? (uint32_t) atoi(dbgEnv) : 0u;

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

    // Optional. Most games never set it, and OptiScaler disables it by default,
    // so treat its absence as normal rather than as a missing input.
    ID3D12Resource* paramReactive = nullptr;
    if (!Config::Instance()->DisableReactiveMask.value_or(true))
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactive) !=
            NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactive);
    }

    if (paramColor == nullptr || paramDepth == nullptr || paramVelocity == nullptr || paramOutput == nullptr)
    {
        LOG_ERROR("Missing inputs: color {0}, depth {1}, mv {2}, output {3}", paramColor != nullptr,
                  paramDepth != nullptr, paramVelocity != nullptr, paramOutput != nullptr);

        return false;
    }

    _hasColor = true;
    _hasDepth = true;
    _hasMV = true;

    {
        auto vd = paramVelocity->GetDesc();
        auto ddesc = paramDepth->GetDesc();
        _constants.depthSize[0] = (uint32_t) ddesc.Width;
        _constants.depthSize[1] = (uint32_t) ddesc.Height;

        // OPTI_SGSR2_NODEPTHFIX=1 feeds the render size instead, reproducing
        // the old mismatched gather exactly, so the depth alignment fix can be
        // A/B'd from a launch option without swapping builds.
        if (const char* e = std::getenv("OPTI_SGSR2_NODEPTHFIX"))
        {
            if (e[0] == '1')
            {
                _constants.depthSize[0] = RenderWidth();
                _constants.depthSize[1] = RenderHeight();
            }
        }

        if (_frameCount == 0)
        {
            auto cd = paramColor->GetDesc();
            LOG_INFO("Inputs: color {0}x{1} fmt {2}, mv {3}x{4} fmt {5}, render {6}x{7}", cd.Width, cd.Height,
                     (int) cd.Format, vd.Width, vd.Height, (int) vd.Format, RenderWidth(), RenderHeight());

            // NGX lets a game point the upscaler at a sub-rectangle of each
            // input texture. A velocity texture larger than the render size
            // does not mean the vectors are display-resolution -- it may just
            // be an oversized allocation with the live region elsewhere. These
            // say which it is, instead of inferring it from the texture size.
            unsigned int mvBaseX = 0, mvBaseY = 0, colBaseX = 0, colBaseY = 0, subW = 0, subH = 0;
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &mvBaseX);
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &mvBaseY);
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &colBaseX);
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &colBaseY);
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &subW);
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &subH);
            LOG_INFO("NGX subrects: mv base ({0},{1}), color base ({2},{3}), render subrect {4}x{5}", mvBaseX, mvBaseY,
                     colBaseX, colBaseY, subW, subH);
        }
    }

    if (_frameCount == 0)
    {
        auto vd = paramVelocity->GetDesc();
        auto cd = paramColor->GetDesc();
        LOG_INFO("Inputs: color {0}x{1} fmt {2}, mv {3}x{4} fmt {5}, render {6}x{7}", cd.Width, cd.Height,
                 (int) cd.Format, vd.Width, vd.Height, (int) vd.Format, RenderWidth(), RenderHeight());
    }
    _hasOutput = true;

    // Must be settled before UpdateConstants: that is where _constants is
    // memcpy'd into the mapped upload buffer for this frame's ring slot.
    //
    // A null SRV is a valid descriptor and reads as zero, so an absent mask
    // needs no special case beyond leaving reactiveStrength at zero.
    DXGI_FORMAT reactiveFormat = DXGI_FORMAT_R8_UNORM;
    if (paramReactive != nullptr)
    {
        reactiveFormat = ResolveFormat(paramReactive->GetDesc().Format);
        // Reuse DLSS's existing reactive-mask knob rather than adding another.
        // 0.35 matches the scale SGSR2's 3-pass variant applies to its own
        // opaque-difference mask (0.35 * 1000 stored, * 0.001 on read).
        const float bias = Config::Instance()->DlssReactiveMaskBias.value_or_default();
        _constants.reactiveStrength = bias > 0.0f ? bias : 0.35f;
    }
    else
    {
        _constants.reactiveStrength = 0.0f;
    }

    // Logged once: whether a game supplies a reactive mask is the thing that
    // decides if this path ever does anything, and it is not visible otherwise.
    if (!_loggedReactive)
    {
        _loggedReactive = true;
        LOG_INFO("Reactive mask: {0} (strength {1})",
                 paramReactive != nullptr
                     ? "present"
                     : (Config::Instance()->DisableReactiveMask.value_or(true) ? "disabled by config" : "not supplied"),
                 _constants.reactiveStrength);
    }

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

    if (paramReactive != nullptr && Config::Instance()->MaskResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramReactive,
                        (D3D12_RESOURCE_STATES) Config::Instance()->MaskResourceBarrier.value(),
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (_constants.debugMode == 5u)
        DumpVelocity(InCommandList, paramVelocity);

    UpdateSameCamera(InCommandList);

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
        ID3D12Resource* srvs[SRV_Count] = { paramColor, paramDepth, paramVelocity, paramReactive };
        DXGI_FORMAT srvFormats[SRV_Count] = { ResolveFormat(paramColor->GetDesc().Format),
                                              ResolveFormat(paramDepth->GetDesc().Format),
                                              ResolveFormat(paramVelocity->GetDesc().Format),
                                              reactiveFormat };

        ID3D12Resource* uavs[UAV_Count] = { _motionDepthClipAlpha, _ycocgColor, _motionCounter };
        DXGI_FORMAT uavFormats[UAV_Count] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_UINT,
                                              DXGI_FORMAT_R32_UINT };

        auto table = BindPass(Device, 0, srvs, srvFormats, uavs, uavFormats);

        InCommandList->SetPipelineState(_convertPipeline);
        InCommandList->SetComputeRootDescriptorTable(1, table);

        auto uavTable = table;
        uavTable.ptr += (UINT64) SRV_Count * _descriptorSize;
        InCommandList->SetComputeRootDescriptorTable(2, uavTable);

        if (_timingEnabled)
            InCommandList->EndQuery(_timestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, _ringIndex * TimestampsPerFrame + 0);

        InCommandList->Dispatch(DivRoundUp(RenderWidth(), kTileSize), DivRoundUp(RenderHeight(), kTileSize), 1);

        if (_timingEnabled)
            InCommandList->EndQuery(_timestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, _ringIndex * TimestampsPerFrame + 1);
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
        ID3D12Resource* srvs[SRV_Count] = { prevHistory, _motionDepthClipAlpha, _ycocgColor, paramReactive };
        DXGI_FORMAT srvFormats[SRV_Count] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                              DXGI_FORMAT_R32_UINT, reactiveFormat };

        // Writing straight into the game's output avoids a full display-resolution
        // copy every frame (~16 MB at 1080p RGBA16F), which is a large cost on a
        // mobile GPU. Fall back to the internal buffer when the game did not
        // create its output with UAV access.
        const bool directToOutput = (paramOutput->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
        ID3D12Resource* finalTarget = directToOutput ? paramOutput : _outputBuffer;

        ID3D12Resource* uavs[UAV_Count] = { finalTarget, nextHistory, _motionCounter };
        DXGI_FORMAT uavFormats[UAV_Count] = { directToOutput ? ResolveFormat(paramOutput->GetDesc().Format)
                                                             : DXGI_FORMAT_R16G16B16A16_FLOAT,
                                              DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_UINT };

        if (directToOutput)
        {
            D3D12_RESOURCE_BARRIER toUav {};
            toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toUav.Transition.pResource = paramOutput;
            toUav.Transition.StateBefore = (D3D12_RESOURCE_STATES) outputState;
            toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            if (outputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                InCommandList->ResourceBarrier(1, &toUav);
        }

        auto table = BindPass(Device, 1, srvs, srvFormats, uavs, uavFormats);

        InCommandList->SetPipelineState(_upscalePipeline);
        InCommandList->SetComputeRootDescriptorTable(1, table);

        auto uavTable = table;
        uavTable.ptr += (UINT64) SRV_Count * _descriptorSize;
        InCommandList->SetComputeRootDescriptorTable(2, uavTable);

        if (_timingEnabled)
            InCommandList->EndQuery(_timestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, _ringIndex * TimestampsPerFrame + 2);

        InCommandList->Dispatch(DivRoundUp(TargetWidth(), kTileSize), DivRoundUp(TargetHeight(), kTileSize), 1);

        if (_timingEnabled)
            InCommandList->EndQuery(_timestampHeap, D3D12_QUERY_TYPE_TIMESTAMP, _ringIndex * TimestampsPerFrame + 3);
    }

    const bool wroteDirect = (paramOutput->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    if (!wroteDirect)
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
        InCommandList->CopyResource(paramOutput, _outputBuffer);
    }

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

        if (wroteDirect)
        {
            barriers[0] = make(_outputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barriers[1] = make(paramOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               (D3D12_RESOURCE_STATES) outputState);
        }
        else
        {
            barriers[0] = make(_outputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barriers[1] = make(paramOutput, D3D12_RESOURCE_STATE_COPY_DEST, (D3D12_RESOURCE_STATES) outputState);
        }
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

    if (_motionCounter != nullptr)
    {
        D3D12_RESOURCE_BARRIER toCopy {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = _motionCounter;
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        InCommandList->ResourceBarrier(1, &toCopy);

        InCommandList->CopyBufferRegion(_motionCounterReadback, (UINT64) _ringIndex * sizeof(uint32_t),
                                        _motionCounter, 0, sizeof(uint32_t));

        std::swap(toCopy.Transition.StateBefore, toCopy.Transition.StateAfter);
        InCommandList->ResourceBarrier(1, &toCopy);
    }

    if (_timingEnabled)
        ResolveTimestamps(InCommandList);

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

    release(_motionCounter);
    release(_motionCounterReadback);
    release(_clearHeap);
    release(_mvReadback);
    release(_timestampHeap);
    release(_timestampReadback);
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
