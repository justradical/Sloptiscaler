#pragma once

#include <upscalers/IFeature.h>

//============================================================================================================
// Snapdragon(TM) Game Super Resolution 2 -- shared, API-independent bits.
//
// SGSR2 is distributed as shader source only (BSD-3-Clause); there is no vendor
// runtime to link, which is exactly why it is viable on ARM64EC where the
// prebuilt x86-64 FidelityFX libraries are not.
//============================================================================================================

// Mirrors the cbuffer in SGSR2_Shaders.h. Grouped into float4-sized blocks so
// HLSL's packing rules and this struct agree without further padding.
struct alignas(16) SGSR2Constants
{
    uint32_t renderSize[2];
    uint32_t displaySize[2];

    float renderSizeRcp[2];
    float displaySizeRcp[2];

    float jitterOffset[2];
    // Folds NGX's MV_Scale together with the pixels->clip-space conversion and
    // the D3D12 Y-sign. See the note in the Convert shader.
    float motionVectorScale[2];

    float clipToPrevClip[16]; // unused while motion comes from the MV texture

    float preExposure;
    float cameraFovAngleHor;
    float cameraNear;
    float minLerpContribution;

    uint32_t bSameCamera;
    uint32_t reset;
    uint32_t depthInverted;
    // Diagnostics only (OPTI_SGSR2_DEBUG=1): replaces the upscaled output with a
    // visualisation of the motion field, so a sparse or mis-scaled velocity
    // buffer can be seen directly instead of inferred from smearing.
    uint32_t debugMode;
    // The game's depth texture is often padded wider than the render size
    // (UE gives 1132x636 for a 1129x636 render), so its UVs need its own size.
    uint32_t depthSize[2];
    uint32_t _pad2[2];
};

static_assert(sizeof(SGSR2Constants) == 160, "SGSR2Constants must match the HLSL cbuffer layout");

class SGSR2Feature : public virtual IFeature
{
  protected:
    SGSR2Constants _constants {};

    // Previous frame's view-projection is not available from the upscaler inputs
    // OptiScaler intercepts, so "same camera" is inferred from motion instead.
    bool _sameCamera = false;

  public:
    feature_version Version() override { return feature_version { 2, 0, 0, 0 }; }
    Upscaler GetUpscalerType() const override { return Upscaler::SGSR2; }

    // SGSR2 ships as shader source and has no vendor runtime to load, so the
    // usual _moduleLoaded bookkeeping never gets set. FeatureProvider checks
    // ModuleLoaded() right after construction and silently swaps the feature for
    // FSR 2.1.2 when it is false, which is why SGSR2 was built and then
    // immediately discarded. There is nothing external to load, so it is always
    // ready.
    bool ModuleLoaded() override { return true; }

    SGSR2Feature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) : IFeature(InHandleId, InParameters)
    {
        // Each feature base is responsible for pulling the render/display
        // resolution and init flags out of the NGX parameters. Without this the
        // dimensions stay zero and InitInternal has nothing to size its
        // resources from.
        _initParameters = SetInitParameters(InParameters);
        _moduleLoaded = true;
    }
};
