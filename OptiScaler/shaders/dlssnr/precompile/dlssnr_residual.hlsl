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
//               history_t = lerp( clamp(reproject(history_{t-1})), edited - original, blend )
//               The per-frame ray-trace noise term of (edited - original) is temporally
//               uncorrelated and averages to zero; the enhancement term follows geometry and
//               persists. The reprojected history is clamped to mean +/- 1.5*sigma of the 3x3
//               neighbourhood of the current edit before the blend -- a TAA-style neighbourhood
//               clamp that collapses reprojection smear at motion boundaries and, because it also
//               snaps zero history into range, fills disocclusions (off-screen / bad MV) without
//               the slow crawl. It is what lets the blend rate stay low without trailing.
//   gMode == 1  Apply: base + delta * gTransferStrength, clamped non-negative. Run after RR+SR
//               with the upscaled history layer as the delta.

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
Texture2D<float4>   gMotion   : register(t3);  // accumulate: normalized current->previous motion, validity in .a.
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
        float4 mv = gMotion.Load(int3(id.xy, 0));
        float2 prevUV = uv + mv.xy;

        bool valid = mv.a > 0.999 && all(isfinite(mv.xy)) &&
                     all(prevUV >= 0.0) && all(prevUV <= 1.0);

        float3 history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : float3(0.0, 0.0, 0.0);
        history = SanitizeFinite3(history, float3(0.0, 0.0, 0.0));

        // Pull the (reprojected, or zero-on-disocclusion) history back into the range this frame's
        // edit supports. Where the reprojection was following the geometry this is a no-op; at a
        // motion boundary it collapses the smear instead of carrying the old edit forward, and at a
        // disocclusion it snaps the empty history straight to the plausible local edit so the pixel
        // does not crawl in over a dozen frames. This clamp is what lets the blend rate stay low.
        history = clamp(history, loClamp, hiClamp);

        // Cold start (first frame / post-cut) is handled by the host passing gResidualBlend = 1.
        float a = clamp(gResidualBlend, 0.0, 1.0);

        gTarget[id.xy] = float4(lerp(history, delta, a), 1.0);
        return;
    }

    if (gMode == 1)
    {
        float4 base  = gSource.Load(int3(id.xy, 0));
        float3 delta = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb, float3(0.0, 0.0, 0.0));

        gTarget[id.xy] = float4(max(base.rgb + delta * gTransferStrength, 0.0), base.a);
        return;
    }

    gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
}
