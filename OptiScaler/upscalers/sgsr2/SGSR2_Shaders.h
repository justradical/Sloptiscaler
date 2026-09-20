#pragma once

//============================================================================================================
// Snapdragon(TM) Game Super Resolution 2 -- HLSL port of the 2-pass compute variant.
//
// Ported from the reference GLSL in SnapdragonStudios/snapdragon-gsr:
//   sgsr/v2/include/glsl_2_pass_cs/sgsr2_convert.comp
//   sgsr/v2/include/glsl_2_pass_cs/sgsr2_upscale.comp
//
//   Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
//   SPDX-License-Identifier: BSD-3-Clause
//
// BSD-3-Clause asks that redistributions of source retain the copyright notice,
// the conditions and the disclaimer, so the upstream licence is reproduced in
// full at LICENSES/BSD-3-Clause-Qualcomm.txt. It is compatible with
// OptiScaler's GPL-3.0; the combined work is GPL-3.0.
//
// The algorithm is unchanged; only the shading-language surface differs:
//   gl_GlobalInvocationID -> SV_DispatchThreadID      texelFetch   -> Load
//   textureGather(t,uv,0) -> t.GatherRed(samp, uv)    textureLod   -> SampleLevel
//   imageStore(i,p,v)     -> i[p] = v                 mix          -> lerp
//
// NDC: REQUEST_NDC_Y_UP is the correct mode for D3D12 (clip-space Y up, texture
// V down). In practice that branch is dead for us -- see the note on
// clipToPrevClip below.
//
// Gather component order: GLSL textureGather and HLSL Gather both return
// (-,+), (+,+), (+,-), (-,-) in .xyzw relative to texel offsets, so the mapping
// is 1:1. Worth knowing this is the usual place a GLSL->HLSL port goes wrong.
//
// clipToPrevClip: SGSR2 only uses it to *derive* motion where the velocity
// texture has no encoded value (EncodedVelocity.x <= 0). OptiScaler always
// supplies full-screen motion vectors from the upscaler inputs it intercepts,
// so every pixel takes the velocity path and the matrix can safely be identity.
//============================================================================================================

// Shared constant buffer. Laid out in explicit float4-sized groups so the HLSL
// packing rules and the C++ struct agree without padding surprises.
#define SGSR2_COMMON_HLSL                                                                                              \
    "cbuffer SGSR2Params : register(b0)\n"                                                                             \
    "{\n"                                                                                                              \
    "    uint2  renderSize;\n"                                                                                         \
    "    uint2  displaySize;\n"                                                                                        \
    "    float2 renderSizeRcp;\n"                                                                                      \
    "    float2 displaySizeRcp;\n"                                                                                     \
    "    float2 jitterOffset;\n"                                                                                       \
    "    float2 motionVectorScale;\n"                                                                                  \
    "    float4 clipToPrevClip[4];\n"                                                                                  \
    "    float  preExposure;\n"                                                                                        \
    "    float  cameraFovAngleHor;\n"                                                                                  \
    "    float  cameraNear;\n"                                                                                         \
    "    float  minLerpContribution;\n"                                                                                \
    "    uint   bSameCamera;\n"                                                                                        \
    "    uint   reset;\n"                                                                                              \
    "    uint   depthInverted;\n"                                                                                      \
    "    uint   debugMode;\n"                                                                                           \
    "    uint2  depthSize;\n"                                                                                           \
    "    uint2  _sgsrPad2;\n"                                                                                          \
    "};\n"                                                                                                             \
    "SamplerState PointClamp  : register(s0);\n"                                                                       \
    "SamplerState LinearClamp : register(s1);\n"

// ----------------------------------------------------------------------------
// Pass 1 -- Convert
//
// Dilates depth over a 3x3 neighbourhood to derive a depth-clip factor, decodes
// (or derives) motion, tonemaps colour into YCoCg packed to R32_UINT.
//
// Inputs : InputColor (render res), InputDepth, InputVelocity
// Outputs: MotionDepthClipAlphaBuffer (RGBA16F), YCoCgColor (R32_UINT)
// ----------------------------------------------------------------------------
inline const char* SGSR2_ConvertShader = SGSR2_COMMON_HLSL R"(
Texture2D<float4> InputColor    : register(t0);
Texture2D<float>  InputDepth    : register(t1);
Texture2D<float4> InputVelocity : register(t2);

RWTexture2D<float4> MotionDepthClipAlphaBuffer : register(u0);
RWTexture2D<uint>   YCoCgColor                 : register(u1);
// One counter of how many sampled pixels are moving, used to spot a static
// camera. See UpdateSameCamera on the host side.
RWBuffer<uint>      MotionCounter              : register(u2);

// Nearest of two depths, honouring reverse-Z.
float Nearer(float a, float b) { return (depthInverted != 0u) ? max(a, b) : min(a, b); }

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= renderSize.x || tid.y >= renderSize.y)
        return;

    float ExposureRcp = preExposure;
    uint2 InputPos    = tid.xy;

    float2 gatherCoord = float2(tid.xy) * renderSizeRcp;
    float2 ViewportUV  = gatherCoord + 0.5f * renderSizeRcp;

    // Nearest-depth dilation, after ffx_fsr2_reconstruct_dilated_velocity_and_previous_depth.h
    //
    // With reverse-Z the nearest surface is the LARGER depth value, so the
    // reference shader's min() has to become max() and the "is anything in
    // front of the far plane" test flips. SGSR2's README calls this out; without
    // it depthclip is inverted, history is kept where it should be rejected, and
    // the result ghosts. OptiScaler reports this per feature via DepthInverted().
    // Gather against the depth texture's own size, not the render size. UE pads
    // render targets -- 1132x636 backing a 1129x636 render -- so UVs built from
    // renderSizeRcp address a 1132-wide texture as if it were 1129 wide and
    // drift up to three texels right by the screen edge. Depth then no longer
    // lines up with colour, and depthclip, which is what rejects stale history,
    // is computed from the wrong surface.
    float2 depthRcp    = float2(1.0f / float(depthSize.x), 1.0f / float(depthSize.y));
    float2 depthCoord  = float2(tid.xy) * depthRcp;
    float4 topleft     = InputDepth.GatherRed(PointClamp, depthCoord);
    float2 v10         = float2(depthRcp.x * 2.0f, 0.0f);
    float4 topRight    = InputDepth.GatherRed(PointClamp, depthCoord + v10);
    float2 v12         = float2(0.0f, depthRcp.y * 2.0f);
    float4 bottomLeft  = InputDepth.GatherRed(PointClamp, depthCoord + v12);
    float2 v14         = float2(depthRcp.x * 2.0f, depthRcp.y * 2.0f);
    float4 bottomRight = InputDepth.GatherRed(PointClamp, depthCoord + v14);

    float maxC        = Nearer(Nearer(Nearer(topleft.y, topRight.x), bottomLeft.z), bottomRight.w);
    float topleft4    = Nearer(Nearer(Nearer(topleft.y, topleft.x), topleft.z), topleft.w);
    float topLeftMax9 = Nearer(bottomLeft.w, Nearer(Nearer(maxC, topleft4), topRight.w));

    float depthclip = 0.0f;
    bool anyGeometry = (depthInverted != 0u) ? (maxC > 1.0e-05f) : (maxC < 1.0f - 1.0e-05f);
    if (anyGeometry)
    {
        float topRight4    = Nearer(Nearer(Nearer(topRight.y, topRight.x), topRight.z), topRight.w);
        float bottomLeft4  = Nearer(Nearer(Nearer(bottomLeft.y, bottomLeft.x), bottomLeft.z), bottomLeft.w);
        float bottomRight4 = Nearer(Nearer(Nearer(bottomRight.y, bottomRight.x), bottomRight.z), bottomRight.w);

        float Wdepth              = 0.0f;
        float Ksep                = 1.37e-05f;
        float Kfov                = cameraFovAngleHor;
        float diagonal_length     = length(float2(renderSize));
        float Ksep_Kfov_diagonal  = Ksep * Kfov * diagonal_length;

        float Depthsep = Ksep_Kfov_diagonal * ((depthInverted != 0u) ? maxC : (1.0f - maxC));
        float EPSILON  = 1.19e-07f;
        Wdepth += saturate(Depthsep / (abs(maxC - topleft4)     + EPSILON));
        Wdepth += saturate(Depthsep / (abs(maxC - topRight4)    + EPSILON));
        Wdepth += saturate(Depthsep / (abs(maxC - bottomLeft4)  + EPSILON));
        Wdepth += saturate(Depthsep / (abs(maxC - bottomRight4) + EPSILON));
        depthclip = saturate(1.0f - Wdepth * 0.25f);
    }

    // SGSR2's reference shader reads a velocity texture in Qualcomm's packed
    // encoding, falling back to clipToPrevClip where no value is present.
    // OptiScaler instead hands us the game's own motion vectors, so convert them
    // directly and skip both the packing and the matrix fallback.
    //
    // SGSR2 wants "Motion" in clip-space units, consumed downstream as
    //     PrevUV = (Hruv.x - 0.5*Motion.x, Hruv.y + 0.5*Motion.y)
    // and OptiScaler supplies vectors that map a current pixel back to its
    // previous position, so with mv in render pixels:
    //     Motion.x = -2 * mv.x / renderWidth
    //     Motion.y = +2 * mv.y / renderHeight   (D3D12: V down, clip Y up)
    // Both factors, plus NGX's MV_Scale, are folded into motionVectorScale on
    // the CPU side so the sign/scale convention lives in exactly one place.
    // Index with InputPos, not tid.xy. Both hold the same value, but tid.xy is
    // also fed to the float conversion used for gatherCoord/ViewportUV, and the
    // compiled SPIR-V then indexes this fetch with that float bit-reinterpreted
    // as an integer:
    //     %79 = OpFunctionCall %float %__1 %63   ; float(tid.x)
    //     %272 = OpBitcast %uint %79             ; used as the texel coordinate
    // float(100) reinterpreted is 1120403456, so every fetch lands out of
    // bounds and returns zero -- which is why motion vectors read as zero in
    // every game, in every SRV slot, for any resource bound. InputColor.Load
    // escaped it only because it already used InputPos.
    float2 rawMv = InputVelocity.Load(int3(InputPos, 0)).xy;

    float2 motion = rawMv * motionVectorScale;

    // debugMode 3: classify the RAW velocity magnitude, before any scaling, so
    // the units are read off directly instead of inferred.
    //   red   |mv| > 1.0     -> pixel-space
    //   green |mv| > 1e-3    -> UV / NDC-space
    //   dim   nonzero but tiny
    //   blue  exactly zero
    if (debugMode == 3u)
    {
        float mag = max(abs(rawMv.x), abs(rawMv.y));
        float3 band;
        if (mag > 1.0f)        band = float3(1.0f, 0.0f, 0.0f);
        else if (mag > 1.0e-3f) band = float3(0.0f, 1.0f, 0.0f);
        else if (mag > 0.0f)    band = float3(0.3f, 0.3f, 0.3f);
        else                    band = float3(0.0f, 0.0f, 1.0f);
        motion = float2(band.x, band.y);
        depthclip = band.z;
    }

    float3 Colorrgb = InputColor.Load(int3(InputPos, 0)).xyz;

    // Simple reversible tonemap; the scale travels in .w so Upscale can undo it.
    float ColorMax = max(max(Colorrgb.x, Colorrgb.y), Colorrgb.z) + ExposureRcp;
    Colorrgb /= ColorMax;

    float3 Colorycocg;
    Colorycocg.x = 0.25f * (Colorrgb.x + 2.0f * Colorrgb.y + Colorrgb.z);
    Colorycocg.y = saturate(0.5f * Colorrgb.x + 0.5f - 0.5f * Colorrgb.z);
    Colorycocg.z = saturate(Colorycocg.x + Colorycocg.y - Colorrgb.x);

    uint x11 = (uint) (Colorycocg.x * 2047.5f);
    uint y11 = (uint) (Colorycocg.y * 2047.5f);
    uint z10 = (uint) (Colorycocg.z * 1023.5f);

    YCoCgColor[tid.xy] = ((x11 << 21u) | (y11 << 10u)) | z10;
    MotionDepthClipAlphaBuffer[tid.xy] = float4(motion, depthclip, ColorMax);

    // One sample per 8x8 block -- about 11k points spread over the frame, which
    // is ample to tell a moving camera from a still one, and cheap enough to
    // avoid a group-shared reduction. A reduction would also need barriers that
    // the early-out above makes unsafe, since returned threads never reach them.
    if (((tid.x & 7u) == 0u) && ((tid.y & 7u) == 0u))
    {
        // A pixel counts as moving at more than ~1/8 pixel of travel, which is
        // below anything visible but above jitter and numerical noise.
        float2 pixels = motion * 0.5f * float2(renderSize);
        if (dot(pixels, pixels) > 0.015f)
        {
            uint prev;
            InterlockedAdd(MotionCounter[0], 1u, prev);
        }
    }
}
)";

// ----------------------------------------------------------------------------
// Pass 2 -- Upscale
//
// Reprojects history by the dilated motion, upsamples the packed YCoCg colour
// with a fast Lanczos kernel over a 5- or 9-tap neighbourhood, clamps history to
// a variance-derived colour box, blends, and converts back to RGB.
//
// PrevHistory and HistoryOutput are ping-ponged every frame by the caller.
//
// Inputs : PrevHistoryOutput (display res), MotionDepthClipAlphaBuffer, YCoCgColor
// Outputs: SceneColorOutput (display res), HistoryOutput (display res)
// ----------------------------------------------------------------------------
inline const char* SGSR2_UpscaleShader = SGSR2_COMMON_HLSL R"(
Texture2D<float4> PrevHistoryOutput          : register(t0);
Texture2D<float4> MotionDepthClipAlphaBuffer : register(t1);
Texture2D<uint>   YCoCgColor                 : register(t2);

RWTexture2D<float4> SceneColorOutput : register(u0);
RWTexture2D<float4> HistoryOutput    : register(u1);
RWBuffer<uint>      MotionCounterUnused : register(u2); // shared root signature

float FastLanczos(float base)
{
    float y      = base - 1.0f;
    float y2     = y * y;
    float y_temp = 0.75f * y + y2;
    return y_temp * y2;
}

float3 DecodeColor(uint sample32)
{
    uint x11 = sample32 >> 21u;
    uint y11 = sample32 & (2047u << 10u);
    uint z10 = sample32 & 1023u;

    float3 samplecolor;
    samplecolor.x = (float) x11 * (1.0f / 2047.5f);
    samplecolor.y = (float) y11 * 4.76953602e-7f - 0.5f;
    samplecolor.z = (float) z10 * (1.0f / 1023.5f) - 0.5f;
    return samplecolor;
}

// One Lanczos tap plus its contribution to the colour box.
#define SGSR2_TAP(packed, offs)                                                     \
{                                                                                   \
    float3 samplecolor  = DecodeColor(packed);                                      \
    float2 baseoffset   = srcpos_srcOutputPos + (offs);                             \
    float  baseoffset_dot = dot(baseoffset, baseoffset);                            \
    float  base         = saturate(baseoffset_dot * kernelbias2);                   \
    float  weight       = FastLanczos(base);                                        \
    Upsampledcw        += float4(samplecolor * weight, weight);                     \
    float  boxweight    = exp(baseoffset_dot * curvebias);                          \
    rectboxmin          = min(rectboxmin, samplecolor);                             \
    rectboxmax          = max(rectboxmax, samplecolor);                             \
    float3 wsample      = samplecolor * boxweight;                                  \
    rectboxcenter      += wsample;                                                  \
    rectboxvar         += samplecolor * wsample;                                    \
    rectboxweight      += boxweight;                                                \
}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= displaySize.x || tid.y >= displaySize.y)
        return;

    float Biasmax_viewportXScale = min((float) displaySize.x / (float) renderSize.x, 1.99f);
    float scalefactor = min(20.0f, pow(((float) displaySize.x / (float) renderSize.x) *
                                       ((float) displaySize.y / (float) renderSize.y), 3.0f));

    float2 HistoryInfoViewportSize = float2(displaySize);
    float2 InputJitter             = jitterOffset;
    float2 InputInfoViewportSize   = float2(renderSize);

    float2 Hruv = (float2(tid.xy) + 0.5f) * displaySizeRcp;
    float2 Jitteruv;
    Jitteruv.x = saturate(Hruv.x + InputJitter.x * renderSizeRcp.x);
    Jitteruv.y = saturate(Hruv.y + InputJitter.y * renderSizeRcp.y);

    int2   InputPos = (int2) (Jitteruv * InputInfoViewportSize);
    float4 mda      = MotionDepthClipAlphaBuffer.SampleLevel(LinearClamp, Jitteruv, 0.0f);
    float2 Motion   = mda.xy;

    float2 PrevUV;
    PrevUV.x = saturate(-0.5f * Motion.x + Hruv.x);
    // REQUEST_NDC_Y_UP (D3D12): +0.5 rather than -0.5 on Y.
    PrevUV.y = saturate(0.5f * Motion.y + Hruv.y);

    float depthfactor = mda.z;
    float ColorMax    = mda.w;

    float4 History      = PrevHistoryOutput.SampleLevel(LinearClamp, PrevUV, 0.0f);
    float3 HistoryColor = History.xyz;
    float  Historyw     = History.w;
    float  Wfactor      = saturate(abs(Historyw));

    float4 Upsampledcw  = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  kernelfactor = saturate(Wfactor + (float) reset);
    float  biasmax      = Biasmax_viewportXScale - Biasmax_viewportXScale * kernelfactor;
    float  biasmin      = max(1.0f, 0.3f + 0.3f * biasmax);
    float  biasfactor   = max(0.25f * depthfactor, kernelfactor);
    float  kernelbias   = lerp(biasmax, biasmin, biasfactor);

    float motion_viewport_len = length(Motion * HistoryInfoViewportSize);
    float curvebias           = lerp(-2.0f, -3.0f, saturate(motion_viewport_len * 0.02f));

    float3 rectboxcenter = float3(0.0f, 0.0f, 0.0f);
    float3 rectboxvar    = float3(0.0f, 0.0f, 0.0f);
    float  rectboxweight = 0.0f;

    float2 srcpos       = float2(InputPos) + 0.5f - InputJitter;
    float2 srcOutputPos = Hruv * InputInfoViewportSize;

    kernelbias *= 0.5f;
    float  kernelbias2        = kernelbias * kernelbias;
    float2 srcpos_srcOutputPos = srcpos - srcOutputPos;

    int2   InputPosBtmRight = int2(1, 1) + InputPos;
    float2 gatherCoord      = float2(InputPos) * renderSizeRcp;

    uint  btmRightTap = YCoCgColor.Load(int3(InputPosBtmRight, 0));
    uint4 topleft     = YCoCgColor.GatherRed(PointClamp, gatherCoord);
    uint2 topRight    = uint2(0, 0);
    uint2 bottomLeft  = uint2(0, 0);

    if (bSameCamera != 0u)
    {
        topRight   = YCoCgColor.GatherRed(PointClamp, gatherCoord + float2(renderSizeRcp.x, 0.0f)).yz;
        bottomLeft = YCoCgColor.GatherRed(PointClamp, gatherCoord + float2(0.0f, renderSizeRcp.y)).xy;
    }
    else
    {
        uint2 br = YCoCgColor.GatherRed(PointClamp, gatherCoord + renderSizeRcp).xz;
        bottomLeft.y = br.x;
        topRight.x   = br.y;
    }

    // First tap seeds the box rather than min/max-ing against it.
    float3 rectboxmin;
    float3 rectboxmax;
    {
        float3 samplecolor    = DecodeColor(bottomLeft.y);
        float2 baseoffset     = srcpos_srcOutputPos + float2(0.0f, 1.0f);
        float  baseoffset_dot = dot(baseoffset, baseoffset);
        float  base           = saturate(baseoffset_dot * kernelbias2);
        float  weight         = FastLanczos(base);
        Upsampledcw          += float4(samplecolor * weight, weight);
        float  boxweight      = exp(baseoffset_dot * curvebias);
        rectboxmin            = samplecolor;
        rectboxmax            = samplecolor;
        float3 wsample        = samplecolor * boxweight;
        rectboxcenter        += wsample;
        rectboxvar           += samplecolor * wsample;
        rectboxweight        += boxweight;
    }

    SGSR2_TAP(topRight.x,  float2( 1.0f,  0.0f))
    SGSR2_TAP(topleft.x,   float2(-1.0f,  0.0f))
    SGSR2_TAP(topleft.y,   float2( 0.0f,  0.0f))
    SGSR2_TAP(topleft.z,   float2( 0.0f, -1.0f))

    if (bSameCamera != 0u)
    {
        SGSR2_TAP(btmRightTap,  float2( 1.0f,  1.0f))
        SGSR2_TAP(bottomLeft.x, float2(-1.0f,  1.0f))
        SGSR2_TAP(topRight.y,   float2( 1.0f, -1.0f))
        SGSR2_TAP(topleft.w,    float2(-1.0f, -1.0f))
    }

    rectboxweight = 1.0f / rectboxweight;
    rectboxcenter *= rectboxweight;
    rectboxvar    *= rectboxweight;
    rectboxvar     = sqrt(abs(rectboxvar - rectboxcenter * rectboxcenter));

    Upsampledcw.xyz = clamp(Upsampledcw.xyz / Upsampledcw.w, rectboxmin - 0.05f, rectboxmax + 0.05f);
    Upsampledcw.w   = Upsampledcw.w * (1.0f / 3.0f);

    float OneMinusWfactor = 1.0f - Wfactor;
    float baseupdate      = OneMinusWfactor - OneMinusWfactor * depthfactor;
    baseupdate = min(baseupdate, lerp(baseupdate, Upsampledcw.w * 10.0f, saturate(10.0f * motion_viewport_len)));
    baseupdate = min(baseupdate, lerp(baseupdate, Upsampledcw.w,        saturate(motion_viewport_len * 0.05f)));
    float basealpha = baseupdate;

    const float EPSILON = 1.192e-07f;
    float  boxscale = max(depthfactor, saturate(motion_viewport_len * 0.05f));
    float  boxsize  = lerp(scalefactor, 1.0f, boxscale);
    float3 sboxvar  = rectboxvar * boxsize;
    float3 boxmin   = rectboxcenter - sboxvar;
    float3 boxmax   = rectboxcenter + sboxvar;
    rectboxmax = min(rectboxmax, boxmax);
    rectboxmin = max(rectboxmin, boxmin);

    float3 clampedcolor   = clamp(HistoryColor, rectboxmin, rectboxmax);
    float  startLerpValue = minLerpContribution;
    if ((abs(mda.x) + abs(mda.y)) > 0.000001f)
        startLerpValue = 0.0f;

    float lerpcontribution = (any(rectboxmin > HistoryColor) || any(HistoryColor > rectboxmax)) ? startLerpValue : 1.0f;

    HistoryColor  = lerp(clampedcolor, HistoryColor, saturate(lerpcontribution));
    float basemin = min(basealpha, 0.1f);
    basealpha     = lerp(basemin, basealpha, saturate(lerpcontribution));

    float alphasum = max(EPSILON, basealpha + Upsampledcw.w);
    float alpha    = saturate(Upsampledcw.w / alphasum + (float) reset);
    Upsampledcw.xyz = lerp(HistoryColor, Upsampledcw.xyz, alpha);

    HistoryOutput[tid.xy] = float4(Upsampledcw.xyz, Wfactor);

    // YCoCg -> RGB, then undo the Convert pass tonemap.
    float x_z = Upsampledcw.x - Upsampledcw.z;
    Upsampledcw.xyz = float3(saturate(x_z + Upsampledcw.y),
                             saturate(Upsampledcw.x + Upsampledcw.z),
                             saturate(x_z - Upsampledcw.y));

    float compMax = max(Upsampledcw.x, Upsampledcw.y);
    compMax = saturate(max(compMax, Upsampledcw.z));
    float scale = preExposure / ((1.0f + 600.0f / 65504.0f) - compMax);

    if (ColorMax > 4000.0f)
        scale = ColorMax;

    Upsampledcw.xyz = Upsampledcw.xyz * scale;

    // Motion-field visualisation. R/G are |Motion| amplified so a typical
    // camera turn is clearly visible, and B flags pixels whose motion is
    // exactly zero. A camera turn over static geometry should light up R/G
    // everywhere; large flat blue regions mean the game left those pixels out
    // of the velocity buffer, in which case the clipToPrevClip fallback that
    // this port dropped is actually required.
    if (debugMode != 0u)
    {
        float2 m = Motion;
        float3 vis;
        if (debugMode == 3u)
        {
            SceneColorOutput[tid.xy] = float4(m.x, m.y, mda.z, 1.0f);
            return;
        }
        // Wine's d3dcompiler has no isnan/isinf at cs_5_0; NaN != itself, and
        // anything past ~1e30 is Inf for our purposes.
        bool bad = (m.x != m.x) || (m.y != m.y) || (abs(m.x) > 1.0e30f) || (abs(m.y) > 1.0e30f);
        if (bad)
            vis = float3(1.0f, 1.0f, 1.0f);           // white: NaN/Inf
        else if (dot(m, m) < 1.0e-12f)
            vis = float3(0.0f, 0.0f, 1.0f);           // blue: exactly zero
        else
            vis = float3(saturate(abs(m.x) * 50.0f), saturate(abs(m.y) * 50.0f), 0.0f);
        SceneColorOutput[tid.xy] = float4(vis, 1.0f);
        return;
    }

    SceneColorOutput[tid.xy] = Upsampledcw;
}
)";
