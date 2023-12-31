/*
Copyright(c) 2016-2024 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 uncharted_2(float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 matrix_movie(float3 keannu)
{
    static const float pow_a = 3.0f / 2.0f;
    static const float pow_b = 4.0f / 5.0f;

    return float3(pow(abs(keannu.r), pow_a), pow(abs(keannu.g), pow_b), pow(abs(keannu.b), pow_a));
}

//==========================================================================================
// ACES
//==========================================================================================

//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//  All code licensed under the MIT license

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 aces_mat_input =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 aces_mat_output =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 aces(float3 color)
{
    color = mul(aces_mat_input, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(aces_mat_output, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

//==========================================================================================
// AMD
//==========================================================================================

// General tonemapping operator, build 'b' term.
float ColToneB(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
    return
        -((-pow(midIn, contrast) + (midOut * (pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) -
            pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut)) /
            (pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut)) /
            (pow(midIn, contrast * shoulder) * midOut));
}

// General tonemapping operator, build 'c' term.
float ColToneC(float hdrMax, float contrast, float shoulder, float midIn, float midOut)
{
    return (pow(hdrMax, contrast * shoulder) * pow(midIn, contrast) - pow(hdrMax, contrast) * pow(midIn, contrast * shoulder) * midOut) /
        (pow(hdrMax, contrast * shoulder) * midOut - pow(midIn, contrast * shoulder) * midOut);
}

// General tonemapping operator, p := {contrast,shoulder,b,c}.
float ColTone(float x, float4 p)
{
    float z = pow(x, p.r);
    return z / (pow(z, p.g) * p.b + p.a);
}

float3 amd(float3 color)
{
    const float hdrMax   = 16.0; // How much HDR range before clipping. HDR modes likely need this pushed up to say 25.0.
    const float contrast = 2.0;  // Use as a baseline to tune the amount of contrast the tonemapper has.
    const float shoulder = 1.0;  // Likely don�t need to mess with this factor, unless matching existing tonemapper is not working well..
    const float midIn    = 0.18; // most games will have a {0.0 to 1.0} range for LDR so midIn should be 0.18.
    const float midOut   = 0.18; // Use for LDR. For HDR10 10:10:10:2 use maybe 0.18/25.0 to start. For scRGB, I forget what a good starting point is, need to re-calculate.

    float b = ColToneB(hdrMax, contrast, shoulder, midIn, midOut);
    float c = ColToneC(hdrMax, contrast, shoulder, midIn, midOut);

    float peak = max(color.r, max(color.g, color.b));
    peak = max(FLT_MIN, peak);

    float3 ratio = color / peak;
    peak = ColTone(peak, float4(contrast, shoulder, b, c));
    // then process ratio

    // probably want send these pre-computed (so send over saturation/crossSaturation as a constant)
    float crosstalk       = 4.0; // controls amount of channel crosstalk
    float saturation      = contrast; // full tonal range saturation control
    float crossSaturation = contrast * 16.0; // crosstalk saturation

    float white = 1.0;

    // wrap crosstalk in transform
    float ratio_temp = saturation / crossSaturation;
    float pow_temp   = pow(peak, crosstalk);
    ratio            = pow(abs(ratio), float3(ratio_temp, ratio_temp, ratio_temp));
    ratio            = lerp(ratio, float3(white, white, white), float3(pow_temp, pow_temp, pow_temp));
    ratio            = pow(abs(ratio), float3(crossSaturation, crossSaturation, crossSaturation));

    // then apply ratio to peak
    color = peak * ratio;
    return color;
}

// HDR10 ST2084 
float3 rec2084_curve_to_color(float3 color, float max_nits)
{
    // constants for PQ transfer function based on ST 2084
    const float m1 = 0.1593017578125f; // (2610 / 4096) * (1 / 4)
    const float m2 = 78.84375f;        // (2523 / 32) * 128
    const float c1 = 0.8359375f;       // (3424 / 4096)
    const float c2 = 18.8515625f;      // (2413 / 128) * 32
    const float c3 = 18.6875f;         // (2392 / 128) * 32

    // calculate the original linear luminance from the color
    float linear_luminance = dot(color, float3(0.2126, 0.7152, 0.0722));

    // scale luminance to be between 0 and 1 based on max_nits
    float scaled_luminance = linear_luminance / max_nits;

    // apply the PQ transfer function to the luminance
    float lp           = pow(scaled_luminance, m1);
    float pq_luminance = pow((c1 + c2 * lp) / (1 + c3 * lp), m2);

    // calculate the ratio of PQ encoded luminance to original luminance
    float luminance_ratio = pq_luminance / linear_luminance;

    // scale the original color by the luminance ratio to get the final color
    return color * luminance_ratio;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    // get cpu data
    float3 f3_value          = pass_get_f3_value();
    float3 f3_value2         = pass_get_f3_value2();
    float luminance_max_nits = f3_value.x;
    float tone_mapping       = f3_value.y;
    float exposure           = f3_value.z;
    float hdr                = f3_value2.x;
   
    // 1. expose
    float4 color = tex[thread_id.xy];
    color.rgb *= exposure;

    // 2. tone-map (needed for SDR, optional for HDR)
    switch (tone_mapping)
    {
        case 0:
            color.rgb = amd(color.rgb);
            break;
        case 1:
            color.rgb = aces(color.rgb);
            break;
        case 2:
            color.rgb = reinhard(color.rgb);
            break;
        case 3:
            color.rgb = uncharted_2(color.rgb);
            break;
        case 4:
            color.rgb = matrix_movie(color.rgb);
            break;
    }

    // 3. linear to color space conversion
    if (hdr != 0.0f) // HDR10 ST2084
    {
        color.rgb = rec2084_curve_to_color(color.rgb, luminance_max_nits);
    }
    else // SDR
    {
        color.rgb = gamma(color.rgb);
    }

    tex_uav[thread_id.xy] = color;
}
