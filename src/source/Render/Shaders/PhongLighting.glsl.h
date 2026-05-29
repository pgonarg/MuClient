#pragma once

// Per-pixel character lighting shaders for MuMain.
// GLSL 1.20 (OpenGL 2.1 compatibility profile).
//
// Design: this is a faithful per-PIXEL re-implementation of the engine's
// existing per-VERTEX lighting from BMD::Transform()/RenderMesh():
//
//     Luminosity = dot(normal, LightPosition) * 0.8 + 0.4   (min-clamped to 0.2)
//     finalColor = texColor * BodyLight * Luminosity
//
// The engine computes that once per vertex (IntensityTransform/LightTransform);
// here we interpolate the bone-rotated normal across the triangle and evaluate
// the same formula per fragment, giving smooth per-pixel shading.
//
// We deliberately use the fixed-function built-ins (gl_Vertex, gl_Normal,
// gl_MultiTexCoord0, gl_ModelViewProjectionMatrix) so that:
//   * vertex positioning is byte-identical to the fixed-function path
//     (no manual matrix capture), and
//   * the engine's existing glVertexPointer/glTexCoordPointer arrays feed the
//     shader unchanged - we only add a glNormalPointer.
// The active texture is whatever RenderMesh already bound to unit 0.

namespace SEASON3B {
namespace Shaders {

// Vertex shader: pass bone-rotated (object-local) normal + texcoord, transform
// position exactly as the fixed-function pipeline would.
const char* PHONG_VERTEX_SHADER = R"(
#version 120

varying vec3 vNormal;      // object-local normal (base directional lighting)
varying vec3 vViewPos;     // view-space position (dynamic point lights)
varying vec3 vViewNormal;  // view-space normal (dynamic point lights)

void main()
{
    // Object-local, bone-rotated normal supplied via glNormalPointer
    // (NormalTransform[]). Base lighting is evaluated in this same space,
    // matching the engine's CPU lighting which also runs pre-world-transform.
    vNormal = gl_Normal;

    // View-space position/normal for dynamic point lights. For characters
    // gl_ModelViewMatrix = View * Object, so this lands in view space - the same
    // space the terrain shader uses, so the light math is shared.
    vViewPos    = vec3(gl_ModelViewMatrix * gl_Vertex);
    vViewNormal = gl_NormalMatrix * gl_Normal;

    // Texture coordinate from glTexCoordPointer (unit 0).
    gl_TexCoord[0] = gl_MultiTexCoord0;

    // Identical transform to the fixed-function pipeline.
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

// Fragment shader: sample the bound texture and apply the engine's lighting
// formula per pixel.
const char* PHONG_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uTexture;   // bound by the engine on texture unit 0
uniform vec3  uLightDir;      // == LightPosition used by BMD::Transform (NOT normalized)
uniform vec3  uBodyLight;     // per-character body/terrain light color
uniform float uAlpha;         // object alpha (fade)

// Dynamic point lights (view space), shared with the terrain shader.
uniform int   uLightCount;
uniform vec3  uLightPos[16];
uniform vec3  uLightColor[16];
uniform float uLightRange[16];

// Directional sun shadow map.
uniform sampler2D uShadowMap;
uniform mat4      uLightFromView;  // view-space -> sun clip space
uniform float     uShadowEnabled;

varying vec3 vNormal;
varying vec3 vViewPos;
varying vec3 vViewNormal;

// Returns 1.0 = fully lit, 0.0 = fully shadowed (5x5 PCF).
float computeShadow()
{
    if (uShadowEnabled < 0.5) return 1.0;
    vec4 lc = uLightFromView * vec4(vViewPos, 1.0);
    if (lc.w <= 0.0) return 1.0;
    vec3 p = lc.xyz / lc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;

    const float bias  = 0.0025;
    const float texel = 1.0 / 2048.0;
    float sum = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y) {
            float closest = texture2D(uShadowMap, p.xy + vec2(float(x), float(y)) * texel).r;
            sum += (p.z - bias > closest) ? 0.0 : 1.0;
        }
    return sum / 25.0;
}

void main()
{
    vec4 texColor = texture2D(uTexture, gl_TexCoord[0].st);

    // Cut out essentially-invisible texels (matches alpha-test behavior for
    // skin/hair cutouts); harmless for opaque body meshes.
    if (texColor.a < 0.04) discard;

    float shadow = computeShadow();

    // Per-pixel re-evaluation of the engine's per-vertex formula:
    //   Luminosity = dot(N, L) * 0.4 + 0.2, min-clamped to 0.15
    // Reduced directional light (0.8 -> 0.4) so dynamic light emitters stand out.
    // The directional (sun) part is gated by the shadow factor; the 0.2 ambient
    // floor stays, so shadowed surfaces darken to ambient rather than black.
    vec3  N = normalize(vNormal);
    float luminosity = dot(N, uLightDir) * 0.4 * shadow + 0.2;
    luminosity = max(luminosity, 0.15);

    // Match fixed-function: the engine set vertex colors (BodyLight * intensity)
    // via glColor, which clamps each component to [0,1] before the texture
    // modulate. Clamp here too so light-emitter meshes (whose BodyLight is
    // boosted past 1.0) don't blow out to over-bright.
    vec3 lightTerm = clamp(uBodyLight * luminosity, 0.0, 1.0);

    // View-space vectors for specular / point lights / rim.
    vec3 Nv = normalize(vViewNormal);
    vec3 V  = normalize(-vViewPos);   // camera sits at the origin in view space

    // ---- Specular / rim tuning (dial these to taste) ----
    const float SHININESS    = 16.0;  // highlight tightness (lower = broader/more visible)
    const float SPEC_KEY     = 0.75;  // sun/key-light specular strength
    const float SPEC_POINT   = 1.1;   // point-light specular strength
    const float RIM_STRENGTH = 0.35;  // fresnel edge-light strength
    const float RIM_POWER    = 2.5;   // edge falloff (lower = wider rim band)

    vec3 spec = vec3(0.0);

    // Key-light (main directional) specular, evaluated in view space. Gated by
    // shadow so there's no sun glint inside shadows.
    vec3 Lkey = normalize(gl_NormalMatrix * uLightDir);
    vec3 Hkey = normalize(Lkey + V);
    spec += uBodyLight * (pow(max(dot(Nv, Hkey), 0.0), SHININESS) * SPEC_KEY * shadow);

    // Dynamic point lights: diffuse + specular, range-attenuated.
    vec3 dyn = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        if (i >= uLightCount) break;
        vec3  Ld   = uLightPos[i] - vViewPos;
        float dist = length(Ld);
        vec3  Ldn  = Ld / max(dist, 0.001);
        float att  = clamp(1.0 - dist / uLightRange[i], 0.0, 1.0);
        att = att * att;
        float ndl  = max(dot(Nv, Ldn), 0.0);
        dyn  += uLightColor[i] * (att * ndl);
        vec3  Hl = normalize(Ldn + V);
        spec += uLightColor[i] * (pow(max(dot(Nv, Hl), 0.0), SHININESS) * SPEC_POINT * att * ndl);
    }

    // Fresnel rim light (brightest at grazing angles), gated by local lighting so
    // it doesn't glow on the fully-dark side.
    float rim = pow(1.0 - max(dot(Nv, V), 0.0), RIM_POWER) * RIM_STRENGTH;

    vec3 diffuseLight = min(lightTerm + dyn, vec3(1.0));
    vec3 color = texColor.rgb * diffuseLight   // lit surface
               + spec * texColor.rgb           // specular highlight (tinted by surface)
               + rim * lightTerm;              // rim edge light
    gl_FragColor = vec4(min(color, vec3(1.0)), texColor.a * uAlpha);
}
)";

// ============================================================================
// Chrome / metal / oil environment-mapping shaders (per-pixel reflection).
//
// The engine's chrome effect derives a reflection texture coordinate from each
// vertex normal (g_chrome[]) and samples a chrome/metal bitmap. Computing that
// coordinate per-VERTEX makes reflections "swim"/facet on low-poly meshes.
// Here we recompute the exact same per-variant formula per-PIXEL from the
// interpolated normal, for smooth reflections. uChromeMode selects the variant
// (matching the priority order in BMD::RenderMesh's g_chrome computation):
//   0=CHROME2 1=CHROME3 2=CHROME4 3=CHROME5 4=CHROME6 5=CHROME7
//   6=OIL     7=CHROME  8=METAL/default
// ============================================================================
const char* CHROME_VERTEX_SHADER = R"(
#version 120

varying vec3 vNormal;
varying vec2 vBaseTex;   // base texcoord, needed by the OIL variant

void main()
{
    vNormal  = gl_Normal;
    vBaseTex = gl_MultiTexCoord0.st;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

const char* CHROME_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uTexture;   // chrome/metal bitmap bound by the engine (unit 0)
uniform vec3  uBodyLight;     // chrome color (BMD::BodyLight, possibly alpha-scaled)
uniform float uAlpha;
uniform int   uChromeMode;
uniform float uWave;          // (WorldTime % 10000) * 0.0001
uniform float uWave2;         // (WorldTime % 5000) * 0.00024 - 0.4
uniform float uWorldTime;     // raw WorldTime (CHROME7)
uniform vec3  uChromeL;       // { cos(WorldTime*0.001), sin(WorldTime*0.002), 1 }
uniform vec2  uBlend;         // blendMeshTextureCoord (CHROME4 / OIL offset)

varying vec3 vNormal;
varying vec2 vBaseTex;

void main()
{
    vec3 N = normalize(vNormal);
    const vec3 LV = vec3(0.0, -0.1, -0.8);   // engine's static LightVector

    vec2 coord;
    if (uChromeMode == 0)        // CHROME2
    {
        coord = vec2((N.z + N.x) * 0.8 + uWave2 * 2.0,
                     (N.y + N.x) * 1.0 + uWave2 * 3.0);
    }
    else if (uChromeMode == 1)   // CHROME3
    {
        float d = dot(N, LV);
        coord = vec2(d, 1.0 - d);
    }
    else if (uChromeMode == 2)   // CHROME4  (+ blend offset)
    {
        float d = dot(N, uChromeL);
        vec2 g = vec2(d, 1.0 - d);
        g.y -= N.z * 0.5 + uWave * 3.0;
        g.x += N.y * 0.5 + uChromeL.y * 3.0;
        coord = g + uBlend;
    }
    else if (uChromeMode == 3)   // CHROME5
    {
        float d = dot(N, uChromeL);
        vec2 g = vec2(d, 1.0 - d);
        g.y -= N.z * 2.5 + uWave * 1.0;
        g.x += N.y * 3.0 + uChromeL.y * 5.0;
        coord = g;
    }
    else if (uChromeMode == 4)   // CHROME6
    {
        float v = (N.z + N.x) * 0.8 + uWave2 * 2.0;
        coord = vec2(v, v);
    }
    else if (uChromeMode == 5)   // CHROME7
    {
        float v = (N.z + N.x) * 0.8 + uWorldTime * 0.00006;
        coord = vec2(v, v);
    }
    else if (uChromeMode == 6)   // OIL  (reflection * base texcoord + blend)
    {
        coord = vec2(N.x, N.y) * vBaseTex + uBlend;
    }
    else if (uChromeMode == 7)   // CHROME
    {
        coord = vec2(N.z * 0.5 + uWave, N.y * 0.5 + uWave * 2.0);
    }
    else                          // METAL / default
    {
        coord = vec2(N.z * 0.5 + 0.2, N.y * 0.5 + 0.5);
    }

    vec4 texColor = texture2D(uTexture, coord);
    // Clamp to [0,1] to match fixed-function's glColor clamp (avoids over-bright
    // on emitters whose BodyLight exceeds 1.0).
    vec3 rgb = texColor.rgb * clamp(uBodyLight, 0.0, 1.0);
    gl_FragColor = vec4(rgb, texColor.a * uAlpha);
}
)";

// ============================================================================
// Terrain shader.
//
// The terrain ground draws immediate-mode quads with a precomputed per-corner
// light color (PrimaryTerrainLight via glColor3fv) modulated against the tile
// texture. Fixed-function already Gouraud-interpolates that color per pixel, so
// this shader faithfully reproduces the same modulate (texColor * vertexColor)
// using the built-in attributes -- it puts terrain on the programmable pipeline
// without changing any of the glColor/glTexCoord/glVertex draw calls.
// ============================================================================
const char* TERRAIN_VERTEX_SHADER = R"(
#version 120

varying vec3 vViewPos;     // view-space position (dynamic point lights)
varying vec3 vViewNormal;  // view-space terrain normal (dynamic point lights)

void main()
{
    gl_FrontColor  = gl_Color;          // per-corner terrain light (Gouraud)
    gl_TexCoord[0] = gl_MultiTexCoord0; // tile texture coordinate

    // Terrain quads are world-space and carry no per-vertex normal, so use the
    // world up-vector (0,0,1) as the ground normal -> view space. This gives a
    // correct circular light pool on flat ground. View-space position uses the
    // same gl_ModelViewMatrix path as the object shader so lights are shared.
    vViewPos    = vec3(gl_ModelViewMatrix * gl_Vertex);
    vViewNormal = gl_NormalMatrix * vec3(0.0, 0.0, 1.0);

    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

const char* TERRAIN_FRAGMENT_SHADER = R"(
#version 120

uniform sampler2D uTexture;

// Dynamic point lights (view space), shared with the object shader.
uniform int   uLightCount;
uniform vec3  uLightPos[16];
uniform vec3  uLightColor[16];
uniform float uLightRange[16];

// Directional sun shadow map.
uniform sampler2D uShadowMap;
uniform mat4      uLightFromView;
uniform float     uShadowEnabled;

varying vec3 vViewPos;
varying vec3 vViewNormal;

float computeShadow()
{
    if (uShadowEnabled < 0.5) return 1.0;
    vec4 lc = uLightFromView * vec4(vViewPos, 1.0);
    if (lc.w <= 0.0) return 1.0;
    vec3 p = lc.xyz / lc.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    const float bias  = 0.0025;
    const float texel = 1.0 / 2048.0;
    float sum = 0.0;
    for (int x = -2; x <= 2; ++x)
        for (int y = -2; y <= 2; ++y) {
            float closest = texture2D(uShadowMap, p.xy + vec2(float(x), float(y)) * texel).r;
            sum += (p.z - bias > closest) ? 0.0 : 1.0;
        }
    return sum / 25.0;
}

void main()
{
    vec4 texColor = texture2D(uTexture, gl_TexCoord[0].st);
    vec4 c = gl_Color;   // static (baked) terrain light, Gouraud-interpolated

    // Darken the baked terrain light where the sun is occluded (not to black).
    c.rgb *= mix(0.45, 1.0, computeShadow());

    // Dynamic per-pixel point lights with range-based attenuation.
    vec3 Nv  = normalize(vViewNormal);
    vec3 dyn = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        if (i >= uLightCount) break;
        vec3  Ld   = uLightPos[i] - vViewPos;
        float dist = length(Ld);
        float att  = clamp(1.0 - dist / uLightRange[i], 0.0, 1.0);
        att = att * att;
        float ndl  = max(dot(Nv, Ld / max(dist, 0.001)), 0.0);
        dyn += uLightColor[i] * (att * ndl);
    }

    vec3 finalLight = min(c.rgb + dyn, vec3(1.0));
    gl_FragColor = vec4(texColor.rgb * finalLight, texColor.a * c.a);
}
)";

}  // namespace Shaders
}  // namespace SEASON3B
