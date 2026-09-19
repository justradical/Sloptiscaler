#pragma once
#include <upscalers/IFeature_Dx11wDx12.h>

// SGSR2 driven from a D3D11 game. The bridge shares the game's D3D11 colour,
// depth and motion textures into D3D12 and then runs the ordinary D3D12
// backend, so nothing in the upscaler itself is duplicated.
class SGSR2FeatureDx11on12 : public IFeature_Dx11wDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::SGSR2_on12; }

    SGSR2FeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
