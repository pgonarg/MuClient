#pragma once

// Screen Space Ambient Occlusion (SSAO) shaders for MuMain.
// GLSL 1.20 (OpenGL 2.1 compatibility profile).
//
// Architecture:
//   1. Capture G-Buffer pass: render scene depth + normals to off-screen FBO
//   2. SSAO pass: sample random directions around each pixel, test occlusion
//   3. Bilateral blur: smooth results while preserving depth edges
//   4. Composite: modulate scene lighting by AO result
//
// SSAO darkens crevices and creases in the geometry, increasing visual depth
// without the performance cost of shadow mapping. Uses screen-space techniques
// (no world-space lookup required).

namespace SEASON3B {
namespace Shaders {

// ============================================================================
// G-Buffer Capture: render depth + normals for SSAO processing
// ============================================================================

const char* SSAO_GBUFFER_VERTEX_SHADER = R"(
#version 120

varying vec3 vViewPos;
varying vec3 vViewNormal;

void main()
{
    // View-space position and normal (same as Phong shader for consistency)
    vViewPos    = vec3(gl_ModelViewMatrix * gl_Vertex);
    vViewNormal = normalize(gl_NormalMatrix * gl_Normal);

    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

const char* SSAO_GBUFFER_FRAGMENT_SHADER = R"(
#version 120

varying vec3 vViewPos;
varying vec3 vViewNormal;

void main()
{
    // Pack view-space position into RGB (XYZ -> RGB, will unpack later)
    // and depth into alpha channel for quick sampling
    float depth = length(vViewPos);  // distance from camera

    // Normalize position to [-1,1] range for storage (will denormalize in SSAO pass)
    // We'll store as [pos.x, pos.y, pos.z, depth] for reconstruction
    vec3 pos_normalized = vViewPos / 100.0;  // scale to store in [0..1] (100m far plane)

    gl_FragColor = vec4(
        pos_normalized * 0.5 + 0.5,  // remap to [0,1] for texture
        depth / 100.0                 // normalized depth
    );
}
)";

// ============================================================================
// SSAO Computation: sample around each pixel, count occlusions
// ============================================================================

const char* SSAO_COMPUTE_VERTEX_SHADER = R"(
#version 120

void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)";

const char* SSAO_COMPUTE_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uGBufferPos;      // G-Buffer: view-space position (RGB) + depth (A)
uniform sampler2D uNoise;           // Random vectors (small repeating texture)
uniform vec2      uNoiseScale;      // Scale factor for noise tiling (screen size / noise size)
uniform float     uRadius;          // Sampling radius in view space (1.0)
uniform float     uBias;            // Self-occlusion prevention (0.025)
uniform int       uSampleCount;     // Number of samples per pixel (8)
uniform float     uIntensity;       // Darkening strength (1.0)
uniform mat4      uProjection;      // Projection matrix for linearizing depth

// Random sample kernel (8 samples evenly distributed around hemisphere)
// These are precomputed normalized directions in tangent space
const vec3 samples[8] = vec3[](
    vec3( 0.707,  0.707,  0.0),   // NE
    vec3( 0.707, -0.707,  0.0),   // SE
    vec3(-0.707,  0.707,  0.0),   // NW
    vec3(-0.707, -0.707,  0.0),   // SW
    vec3( 0.0,    0.707,  0.707), // N-up
    vec3( 0.0,   -0.707,  0.707), // S-up
    vec3( 0.707,  0.0,    0.707), // E-up
    vec3(-0.707,  0.0,    0.707)  // W-up
);

void main()
{
    vec2 texCoord = gl_TexCoord[0].xy;

    // Unpack G-Buffer: position and depth
    vec4 gBuffer = texture2D(uGBufferPos, texCoord);
    vec3 fragPos = (gBuffer.rgb * 2.0 - 1.0) * 100.0;  // denormalize
    float fragDepth = gBuffer.a * 100.0;                // denormalize depth

    // Sample random rotation for sample kernel (tile noise texture)
    vec3 randomVec = texture2D(uNoise, texCoord * uNoiseScale).rgb;

    // Build local coordinate frame from normal (Gram-Schmidt orthogonalization)
    vec3 normal = normalize(randomVec);  // Simplified: use noise as approximation
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);

    // Compute occlusion by sampling around this pixel
    float occlusion = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= uSampleCount) break;

        // Rotate sample direction by random tangent frame
        vec3 sampleDir = tangent * samples[i].x
                       + bitangent * samples[i].y
                       + normal * samples[i].z;

        // Sample position at offset
        vec3 samplePos = fragPos + sampleDir * uRadius;

        // Project to screen space (simplified: assume similar depth)
        vec2 sampleTexCoord = texCoord + normalize(sampleDir.xy) * 0.1;  // fixed offset

        // Compare: is sample occluded by neighbor?
        vec4 sampleGBuffer = texture2D(uGBufferPos, sampleTexCoord);
        if (sampleGBuffer.a > 0.0) {
            vec3 sampleFragPos = (sampleGBuffer.rgb * 2.0 - 1.0) * 100.0;
            float sampleDepth = sampleGBuffer.a * 100.0;

            // If neighbor is closer (in front), we're occluded
            float rangeCheck = step(abs(fragDepth - sampleDepth), uRadius);
            occlusion += rangeCheck * (sampleDepth > fragDepth + uBias ? 1.0 : 0.0);
        }
    }

    // Normalize occlusion value [0..1]
    occlusion = occlusion / float(uSampleCount);

    // Apply intensity and invert (0=occluded, 1=visible)
    float ao = 1.0 - (occlusion * uIntensity);

    gl_FragColor = vec4(vec3(ao), 1.0);
}
)";

// ============================================================================
// Bilateral Blur: smooth AO while preserving depth edges
// ============================================================================

const char* SSAO_BLUR_VERTEX_SHADER = R"(
#version 120

void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)";

const char* SSAO_BLUR_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uAO;          // Raw SSAO result
uniform sampler2D uGBufferPos;  // For edge detection
uniform vec2      uTexelSize;   // 1.0 / screen resolution
uniform float     uBlurRadius;  // Blur kernel radius (pixels)

void main()
{
    vec2 texCoord = gl_TexCoord[0].xy;
    vec4 centerGBuffer = texture2D(uGBufferPos, texCoord);
    float centerDepth = centerGBuffer.a;

    float result = 0.0;
    float weights = 0.0;

    // 9-tap bilateral blur: skip samples too far in depth (edges)
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * uTexelSize * uBlurRadius;
            vec2 sampleCoord = texCoord + offset;

            float sampleAO = texture2D(uAO, sampleCoord).r;
            vec4 sampleGBuffer = texture2D(uGBufferPos, sampleCoord);
            float sampleDepth = sampleGBuffer.a;

            // Depth similarity (edge-preserving weight)
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * 100.0);  // sharp cutoff at depth discontinuities

            // Spatial weight (gaussian falloff)
            float dist = length(vec2(float(x), float(y)));
            float spatialWeight = exp(-(dist * dist) / 2.0);

            float weight = depthWeight * spatialWeight;
            result += sampleAO * weight;
            weights += weight;
        }
    }

    result /= weights;
    gl_FragColor = vec4(vec3(result), 1.0);
}
)";

// ============================================================================
// Composite: modulate scene by AO (final integration)
// ============================================================================

const char* SSAO_COMPOSITE_VERTEX_SHADER = R"(
#version 120

void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)";

const char* SSAO_COMPOSITE_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uScene;   // Original lit scene
uniform sampler2D uAO;      // Blurred SSAO result

void main()
{
    vec3 sceneColor = texture2D(uScene, gl_TexCoord[0].xy).rgb;
    float ao = texture2D(uAO, gl_TexCoord[0].xy).r;

    // Darken scene by AO: multiply lighting by occlusion factor
    // AO ranges [0..1], where 0 = fully occluded (black), 1 = fully lit
    vec3 finalColor = sceneColor * ao;

    gl_FragColor = vec4(finalColor, 1.0);
}
)";

}  // namespace Shaders
}  // namespace SEASON3B
