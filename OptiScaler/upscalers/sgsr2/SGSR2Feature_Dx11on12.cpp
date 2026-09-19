#include <pch.h>

#include "SGSR2Feature_Dx11on12.h"
#include "SGSR2Feature_Dx12.h"

SGSR2FeatureDx11on12::SGSR2FeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_Dx11wDx12(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<SGSR2FeatureDx12>(InHandleId, InParameters);
}
