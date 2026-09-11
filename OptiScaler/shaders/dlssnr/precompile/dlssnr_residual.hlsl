// ResidualAcrossRR v2 -- the MV-reprojected temporal accumulator for the pre-SR NR residual.
//
// Deliberately a SEPARATE shader from dlssnr.hlsl. Regenerating dlssnr.hlsl's blob with a current
// dxc produces materially different DXIL from the committed one (older compiler), and that shader
// carries every NR path -- post-SR, pre-SR, RR, DeferredDLSS, ResidualFG. These two experimental
// modes get their own tiny blob and a second compute PSO instead, so the battle-tested one is
// never touched. The cbuffer and bindings mirror dlssnr.hlsl exactly so DlssNr_Dx12's existing
// root signature and descriptor table are reused as-is; only gResidualBlend is appended, and it
// fits inside DlssNrConstants' existing 256-byte alignment with no size change.
//
//   gMode == 0  Accumulate: (edited - original) blended into the MV-reprojected history layer.
//               history_t = lerp( reproject(history_{t-1}), edited - original, blend )
//               The per-frame ray-trace noise term of (edited - original) is temporally
//               uncorrelated and averages to zero; the enhancement term follows geometry and
//               persists. Invalid reprojection (off-screen / bad MV) -> the
//               history is treated as zero at that pixel and rebuilds over the next frames.
//               Before the blend, the reprojected history is clamped to mean +/- 1.5*sigma of the
//               current delta's own 3x3 neighbourhood -- a TAA-style neighbourhood clamp that
//               collapses reprojection smear at motion boundaries (it also pulls zero/invalid
//               history into range, so it doubles as the disocclusion fade-in). This is what lets
//               the blend rate stay low without trailing on fast camera motion.
//   gMode == 1  Apply: base + delta * gTransferStrength, clamped non-negative. Run after RR+SR
//               with the upscaled history layer as the delta. Plain-resample fallback for when the
//               private DLSS SR carrier upscale (modes 2/3) is unavailable or fails.
//   gMode == 2  EncodeCarrier: the accumulated history, reversibly compressed to a neutral-0.5
//               carrier in [0,1] (same compression as dlssnr.hlsl's EncodeResidual), so a private
//               NVIDIA DLSS SR feature can upscale it using the game's real depth and motion
//               vectors instead of a plain resample -- genuine neural reconstruction of the
//               carried edit at output resolution, not a blur of a render-resolution layer.
//   gMode == 3  ApplyCarrier: decode the private feature's upscaled carrier (same decompression as
//               dlssnr.hlsl's ApplyResidual) and add it to the finished RR+SR frame, scaled by
//               gTransferStrength, clamped non-negative.

#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;
    float gMvScaleY;
    uint  gGuideWidth;
    uint  gGuideHeight;
    uint  gCompareMode;
    float gCompareSplit;
    float gCompareZoom;
    uint  gCompareSwap;
    uint  gTransfer;
    float gDebugScale;
    uint  gReversibleMode;
    uint  gApplyModel;
    uint  gUseGameExposure;
    float gExposurePreMul;
    uint  gSkinProtection;
    uint  gShowSkinMask;
    float gSkinDetail;
    float gSkinColour;
    float gEnvironmentDetail;
    float gEnvironmentColour;
    float gResidualBlend;   // v2 only: history blend rate, 0..1. 1 == no accumulation (== v1).
    uint gResidualHistoryValid;
    uint gResidualMotionBaseX;
    uint gResidualMotionBaseY;
};

// Same registers and the same SPIR-V binding numbers as dlssnr.hlsl, including the slots these
// modes do not read (gExposure t4, gKeep u1) -- DispatchResidualPass binds a stand-in into them
// exactly as DispatchPass does, and a future Vulkan host path needs the numbering to line up.
#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4>   gSource   : register(t0);  // accumulate: the untouched pre-SR frame. apply: the RR+SR output.
#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float4>   gModel    : register(t1);  // accumulate: the NR-edited frame. apply: the upscaled delta layer.
#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float4>   gOriginal : register(t2);  // accumulate: the previous history layer.
#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
Texture2D<float4>   gMotion   : register(t3);  // raw game motion; active size, offsets and scale come from the host.
#ifndef VK_MODE
Texture2D<float4>   gExposure : register(t4);  // unused here; bound for descriptor-table parity.
#endif
#ifdef VK_MODE
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> gTarget   : register(u0);  // accumulate: the new history layer. apply: the composed frame.
#ifdef VK_MODE
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> gKeep     : register(u1);  // unused here; bound for descriptor-table parity.
#ifdef VK_MODE
[[vk::binding(7, 0)]]
#endif
SamplerState        gLinear   : register(s0);  // history is sampled at the reprojected coordinate.

float  SanitizeFinite(float v, float fallback)   { return isfinite(v) ? v : fallback; }
float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

// The model's edit for one pixel: the edited frame minus the untouched one.
float3 LoadDelta(int2 p)
{
    return SanitizeFinite3(gModel.Load(int3(p, 0)).rgb - gSource.Load(int3(p, 0)).rgb,
                           float3(0.0, 0.0, 0.0));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    if (gMode == 0)
    {
        float3 delta = LoadDelta(int2(id.xy));

        // Mean and spread of the edit over the 3x3 neighbourhood. The edit is noise-dominated per
        // frame (delta ~ enhancement - n_t), so a hard min/max box would be as wide as the noise;
        // mean +/- k*sigma tracks the local enhancement and only opens up where the neighbourhood
        // genuinely disagrees (an edge, a thin feature).
        float3 m1 = float3(0.0, 0.0, 0.0);
        float3 m2 = float3(0.0, 0.0, 0.0);
        [unroll] for (int oy = -1; oy <= 1; ++oy)
        {
            [unroll] for (int ox = -1; ox <= 1; ++ox)
            {
                int2 p = clamp(int2(id.xy) + int2(ox, oy), int2(0, 0),
                               int2((int) gWidth - 1, (int) gHeight - 1));
                float3 n = LoadDelta(p);
                m1 += n;
                m2 += n * n;
            }
        }
        m1 /= 9.0;
        m2 /= 9.0;
        float3 sigma   = sqrt(max(m2 - m1 * m1, float3(0.0, 0.0, 0.0)));
        float3 loClamp = m1 - 1.5 * sigma;
        float3 hiClamp = m1 + 1.5 * sigma;

        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        uint2 guideSize = uint2(gGuideWidth, gGuideHeight);
        uint2 guidePos = min(uint2(uv * guideSize), guideSize - 1) +
                         uint2(gResidualMotionBaseX, gResidualMotionBaseY);
        float2 motion = gMotion.Load(int3(guidePos, 0)).xy * float2(gMvScaleX, gMvScaleY);
        float2 prevUV = uv + motion;

        bool valid = gResidualHistoryValid != 0 && all(isfinite(motion)) && all(abs(motion) < 2.0) &&
                     all(prevUV >= 0.0) && all(prevUV <= 1.0);

        float3 history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : float3(0.0, 0.0, 0.0);
        history = SanitizeFinite3(history, float3(0.0, 0.0, 0.0));

        // Pull the (reprojected, or zero-on-disocclusion) history back into the range this frame's
        // edit supports. Where the reprojection was following the geometry this is a no-op; at a
        // motion boundary it collapses the smear instead of carrying the old edit forward, and at a
        // disocclusion it snaps the empty history straight to the plausible local edit so the pixel
        // does not crawl in over a dozen frames. This clamp is what lets the blend rate stay low.
        history = clamp(history, loClamp, hiClamp);

        float a = clamp(gResidualBlend, 0.0, 1.0);

        gTarget[id.xy] = float4(lerp(history, delta, a), 1.0);
        return;
    }

    if (gMode == 1)
    {
        float4 base  = gSource.Load(int3(id.xy, 0));
        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        float3 delta = SanitizeFinite3(gModel.SampleLevel(gLinear, uv, 0).rgb, float3(0.0, 0.0, 0.0));

        gTarget[id.xy] = float4(max(base.rgb + delta * gTransferStrength, 0.0), base.a);
        return;
    }

    // Neutral 0.5 encodes zero; values below it carry darkening. A reversible signed compression
    // avoids clipping negative edits at the private DLSS feature's input, which expects an
    // LDR-biased [0,1] picture. Mirrors dlssnr.hlsl's EncodeResidual (mode 5) exactly, except the
    // difference is already computed -- gSource here is the accumulator's own signed history, not
    // two frames to subtract.
    if (gMode == 2)
    {
        float3 delta = SanitizeFinite3(gSource.Load(int3(id.xy, 0)).rgb, float3(0.0, 0.0, 0.0));
        float3 d = delta / max(gExposurePreMul, 1e-4);
        gTarget[id.xy] = float4(0.5 + 0.5 * d / (1.0 + abs(d)), 1.0);
        return;
    }

    // Decode the private feature's upscaled carrier and add it to the finished RR+SR frame.
    // Mirrors dlssnr.hlsl's ApplyResidual (mode 6): the inverse of EncodeCarrier above, limited
    // near its poles because DLSS can ring outside the carrier's [0,1] range.
    if (gMode == 3)
    {
        float4 base = gSource.Load(int3(id.xy, 0));
        float3 encoded = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb, float3(0.5, 0.5, 0.5));
        float3 signedEdit = clamp(2.0 * encoded - 1.0, -0.999, 0.999);
        float3 delta = signedEdit / (1.0 - abs(signedEdit)) * max(gExposurePreMul, 1e-4);

        gTarget[id.xy] = float4(max(base.rgb + delta * gTransferStrength, 0.0), base.a);
        return;
    }

    gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
}
