#pragma once

#include "SGSR2Feature.h"
#include <upscalers/IFeature_Dx12.h>

#include <d3d12.h>

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
