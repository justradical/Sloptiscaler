#include "pch.h"
#include "SGSR2Feature.h"

#include <Config.h>
#include <Util.h>
#include <MathUtils.h>

#include <cmath>
#include <cstdlib>

// Everything the two backends agree on. Only the upload differs between them --
// a mapped D3D12 upload heap vs a mapped Vulkan buffer -- so each backend calls
// this and then copies _constants into its own ring slot.
//
// This lives in one place deliberately. The motion-vector sign and scale
// convention in particular took a long time to pin down against real games, and
// a second copy of it would be free to drift.
bool SGSR2Feature::UpdateSharedConstants(NVSDK_NGX_Parameter* InParameters)
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


    return true;
}
