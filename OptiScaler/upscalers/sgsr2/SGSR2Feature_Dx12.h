#pragma once

#include "SGSR2Feature.h"
#include <upscalers/IFeature_Dx12.h>

#include <d3d12.h>
#include <chrono>

//============================================================================================================
// SGSR2 on D3D12.
//
// Two compute passes over resources OptiScaler already intercepts:
//
//   Convert : InputColor + InputDepth + InputVelocity (render res)
//             -> MotionDepthClipAlphaBuffer (RGBA16F) + YCoCgColor (R32_UINT)
//   Upscale : PrevHistory + the two above
//             -> SceneColorOutput (display res) + HistoryOutput (display res)
//
// History is ping-ponged between two display-resolution buffers.
//
// Both passes share one root signature, since their binding shape is identical:
//   b0 = constants, t0..t2 = SRVs, u0..u1 = UAVs, with two static samplers.
//============================================================================================================

class SGSR2FeatureDx12 : public SGSR2Feature, public IFeature_Dx12
{
  private:
    enum SrvSlot
    {
        // Convert
        SRV_Color = 0,
        SRV_Depth = 1,
        SRV_Velocity = 2,
        // Upscale (same table, rebound between dispatches)
        SRV_PrevHistory = 0,
        SRV_MotionDepthClip = 1,
        SRV_YCoCg = 2,
        SRV_Count = 3,
    };

    enum UavSlot
    {
        UAV_Count = 2,
    };

    // Descriptors consumed per frame: one SRV+UAV set per pass.
    static constexpr uint32_t DescriptorsPerPass = SRV_Count + UAV_Count;
    static constexpr uint32_t DescriptorsPerFrame = DescriptorsPerPass * 2;
    // Ring depth, so descriptors are not rewritten while still in flight.
    static constexpr uint32_t FrameRingDepth = 4;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _convertPipeline = nullptr;
    ID3D12PipelineState* _upscalePipeline = nullptr;

    ID3D12DescriptorHeap* _descriptorHeap = nullptr;
    uint32_t _descriptorSize = 0;
    uint32_t _ringIndex = 0;

    ID3D12Resource* _constantBuffer = nullptr;
    uint8_t* _constantBufferMapped = nullptr;

    ID3D12Resource* _motionDepthClipAlpha = nullptr;
    ID3D12Resource* _ycocgColor = nullptr;
    ID3D12Resource* _history[2] = { nullptr, nullptr };
    ID3D12Resource* _outputBuffer = nullptr;
    uint32_t _historyIndex = 0;
    bool _historyValid = false;

    // GPU timing, opt-in via OPTI_SGSR2_TIMING=1.
    static constexpr uint32_t TimestampsPerFrame = 4;
    ID3D12QueryHeap* _timestampHeap = nullptr;
    ID3D12Resource* _timestampReadback = nullptr;
    bool _timingEnabled = false;
    uint64_t _timerFrequency = 0;
    double _timingPass1Ms = 0.0;
    double _timingPass2Ms = 0.0;
    double _timingWorstMs = 0.0;
    uint32_t _timingSamples = 0;
    std::chrono::steady_clock::time_point _timingLastEvaluate {};
    double _timingWallMs = 0.0;
    uint32_t _timingWallSamples = 0;

    void ResolveTimestamps(ID3D12GraphicsCommandList* InCommandList);

    // OPTI_SGSR2_DEBUG=5: copy the velocity texture to system memory once and
    // report what is in it, split by the NGX subrect. A shader reading zero and
    // a texture containing zero are indistinguishable from the GPU side.
    ID3D12Resource* _mvReadback = nullptr;
    uint64_t _mvReadbackPitch = 0;
    uint32_t _mvReadbackW = 0;
    uint32_t _mvReadbackH = 0;
    DXGI_FORMAT _mvReadbackFormat = DXGI_FORMAT_UNKNOWN;
    int _mvReadbackState = 0;
    uint32_t _mvReadbackFrame = 0;

    void DumpVelocity(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* velocity);

    bool CreatePipelines(ID3D12Device* device);
    bool CreateResources(ID3D12Device* device);
    void ReleaseResources();

    bool UpdateConstants(NVSDK_NGX_Parameter* InParameters);

    D3D12_GPU_DESCRIPTOR_HANDLE BindPass(ID3D12Device* device, uint32_t passIndex, ID3D12Resource* const* srvs,
                                         const DXGI_FORMAT* srvFormats, ID3D12Resource* const* uavs,
                                         const DXGI_FORMAT* uavFormats);

  public:
    SGSR2FeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : SGSR2Feature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters),
          IFeature(InHandleId, InParameters)
    {
    }

    bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return SGSR2Feature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::SGSR2; }
    bool IsWithDx12() final { return false; }

    ~SGSR2FeatureDx12() override;
};
