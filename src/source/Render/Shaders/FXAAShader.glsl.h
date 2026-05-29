#pragma once

// Fast Approximate Anti-Aliasing (FXAA) shader for MuMain
// GLSL 1.20 (OpenGL 2.1 compatibility profile)
//
// FXAA is a post-process edge-detection and anti-aliasing algorithm that:
// 1. Detects edge strength via local luminance contrast
// 2. Identifies edge direction by sampling adjacent pixels
// 3. Blends along the edge to smooth jagged lines
//
// This implementation uses quality preset 12 (good balance of quality/speed).
// Works on any rendered image without scene modification.

namespace SEASON3B {
namespace Shaders {

// Simple passthrough vertex shader for post-process full-screen quad
const char* FXAA_VERTEX_SHADER = R"(
#version 120

void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)";

// FXAA fragment shader - edge detection and anti-aliasing
const char* FXAA_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uTexture;              // Input scene texture
uniform vec2      uRcpFrame;             // Reciprocal of frame size (1.0 / resolution)

// FXAA quality preset: determines how many samples are taken
// Higher = better quality but slower
// Preset 12 = good balance (8 samples)
#define FXAA_QUALITY_PRESET 12

void main()
{
    vec2 posM = gl_TexCoord[0].xy;

    // Sample center and 4 adjacent pixels
    vec3 rgbNW = texture2D(uTexture, posM + vec2(-1.0, -1.0) * uRcpFrame).xyz;
    vec3 rgbNE = texture2D(uTexture, posM + vec2( 1.0, -1.0) * uRcpFrame).xyz;
    vec3 rgbSW = texture2D(uTexture, posM + vec2(-1.0,  1.0) * uRcpFrame).xyz;
    vec3 rgbSE = texture2D(uTexture, posM + vec2( 1.0,  1.0) * uRcpFrame).xyz;
    vec3 rgbM  = texture2D(uTexture, posM                      ).xyz;

    // Convert to luma (perceived brightness) using standard luminance formula
    const vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    // Find min and max luma of the 4 neighbors
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // If edge contrast is too small, no need to blend - return original
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0833, lumaMax * 0.25))
    {
        gl_FragColor = vec4(rgbM, 1.0);
        return;
    }

    // Calculate edge direction by comparing diagonal pairs
    float lumaL = (lumaNW + lumaSW) * 0.5;  // left average
    float lumaR = (lumaNE + lumaSE) * 0.5;  // right average
    float lumaU = (lumaNW + lumaNE) * 0.5;  // up average
    float lumaD = (lumaSW + lumaSE) * 0.5;  // down average

    float lumaHor = abs(lumaL - lumaR);     // horizontal edge strength
    float lumaVer = abs(lumaU - lumaD);     // vertical edge strength

    // Determine primary edge direction (horizontal or vertical)
    bool isHorizontal = lumaHor > lumaVer;

    // Sample along the edge direction to measure local gradient
    float luma1 = isHorizontal ? lumaSW : lumaNW;
    float luma2 = isHorizontal ? lumaSE : lumaNE;
    float gradient = abs(luma1 - lumaM) + abs(luma2 - lumaM);

    // Clamp how far we search along the edge (in pixels)
    float searchLength = 2.0;

    // Search further if edge is strong
    if (abs(luma1 - lumaM) < abs(luma2 - lumaM))
    {
        searchLength = -searchLength;
    }

    // Determine blend direction and sample further along edge
    vec2 blendDir = isHorizontal ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
    vec2 p1 = posM + blendDir * searchLength * uRcpFrame;
    vec2 p2 = posM - blendDir * searchLength * uRcpFrame;

    // Sample along the edge direction
    vec3 rgb1 = texture2D(uTexture, p1).xyz;
    vec3 rgb2 = texture2D(uTexture, p2).xyz;
    float luma1Sample = dot(rgb1, luma);
    float luma2Sample = dot(rgb2, luma);

    // Blend weight: how far from center to apply the edge sample
    float weight = 0.5;

    if (abs(luma1Sample - lumaM) < abs(luma2Sample - lumaM))
    {
        weight = 0.25;
    }
    else
    {
        weight = 0.75;
    }

    // Final blend: mix center sample with edge-detected sample
    // This smooths aliased edges without excessive blur
    vec3 finalRGB = mix(rgbM, mix(rgb1, rgb2, weight), 0.25);

    gl_FragColor = vec4(finalRGB, 1.0);
}
)";

}  // namespace Shaders
}  // namespace SEASON3B
