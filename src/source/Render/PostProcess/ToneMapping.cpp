///////////////////////////////////////////////////////////////////////////////
// ToneMapping.cpp — tone mapping (HDR → LDR) post-process
//
// Converts the scene from HDR range to displayable LDR [0..1] using Reinhard
// tone mapping with gamma correction. Integrates into the post-processing chain
// after Bloom and SSAO.
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ToneMapping.h"
#include <cstdio>
#include <cstring>

// ─── static member definitions ──────────────────────────────────────────────

bool           ToneMapping::s_initialized      = false;
bool           ToneMapping::s_enabled          = false;
float          ToneMapping::s_strength         = 1.0f;
float          ToneMapping::s_gamma            = 2.2f;
float          ToneMapping::s_exposure         = 0.85f;      // Subtle darken, preserve color
float          ToneMapping::s_tintR            = 0.98f;      // Minimal desaturate
float          ToneMapping::s_tintG            = 0.98f;
float          ToneMapping::s_tintB            = 1.0f;
float          ToneMapping::s_contrast         = 1.1f;       // Subtle boost, no crushing
unsigned int   ToneMapping::s_toneMappingProgram = 0;
unsigned int   ToneMapping::s_sceneFbo         = 0;
unsigned int   ToneMapping::s_sceneColorTex    = 0;
int            ToneMapping::s_fboWidth         = 0;
int            ToneMapping::s_fboHeight        = 0;
int            ToneMapping::s_currentPreset    = 1;  // Start with PresetSubtleDark

// ─── GLSL source strings ─────────────────────────────────────────────────────

static const char* kToneMappingVertSrc = R"GLSL(
#version 120
void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)GLSL";

// Reinhard tone mapping with exposure, color tint, contrast, and gamma correction
// Input: scene color (may be out of [0..1] range due to lighting)
// Output: tone-mapped and color-graded color suitable for display
static const char* kToneMappingFragSrc = R"GLSL(
#version 120
uniform sampler2D uScene;
uniform float     uStrength;
uniform float     uGamma;
uniform float     uExposure;
uniform vec3      uColorTint;
uniform float     uContrast;

void main()
{
    vec3 color = texture2D(uScene, gl_TexCoord[0].xy).rgb;

    // Apply exposure adjustment
    color *= uExposure;

    // Reinhard tone mapping: compress HDR range to [0..1]
    vec3 toneMapped = color / (color + vec3(1.0));

    // Blend between original and tone-mapped based on strength
    vec3 result = mix(color, toneMapped, uStrength);

    // Apply color tint for mood
    result *= uColorTint;

    // Apply contrast boost (center at 0.5)
    result = mix(vec3(0.5), result, uContrast);

    // Apply gamma correction (inverse of display gamma)
    float invGamma = 1.0 / uGamma;
    result = pow(result, vec3(invGamma));

    // Clamp to [0..1] for display
    result = clamp(result, 0.0, 1.0);

    gl_FragColor = vec4(result, 1.0);
}
)GLSL";

// ─── shader helpers ───────────────────────────────────────────────────────────

unsigned int ToneMapping::CompileShader(unsigned int type, const char* src)
{
    GLuint s = glCreateShader(static_cast<GLenum>(type));
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        char msg[600];
        sprintf_s(msg, sizeof(msg), "ToneMapping: shader compile error:\n%s\n", log);
        OutputDebugStringA(msg);
        glDeleteShader(s);
        return 0;
    }

    return s;
}

unsigned int ToneMapping::LinkProgram(unsigned int vert, unsigned int frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        char msg[600];
        sprintf_s(msg, sizeof(msg), "ToneMapping: program link error:\n%s\n", log);
        OutputDebugStringA(msg);
        glDeleteProgram(prog);
        return 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

bool ToneMapping::CompileShaders()
{
    unsigned int vert = CompileShader(GL_VERTEX_SHADER, kToneMappingVertSrc);
    if (!vert) return false;

    unsigned int frag = CompileShader(GL_FRAGMENT_SHADER, kToneMappingFragSrc);
    if (!frag)
    {
        glDeleteShader(vert);
        return false;
    }

    s_toneMappingProgram = LinkProgram(vert, frag);
    return s_toneMappingProgram != 0;
}

void ToneMapping::DeleteShaders()
{
    if (s_toneMappingProgram)
    {
        glDeleteProgram(s_toneMappingProgram);
        s_toneMappingProgram = 0;
    }
}

void ToneMapping::RenderFullscreenQuad()
{
    // Immediate-mode fullscreen quad using fixed-function vertex attributes
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(1, 0);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(0, 1);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool ToneMapping::Initialize()
{
    if (s_initialized) return true;

    if (!CompileShaders())
    {
        OutputDebugStringA("ToneMapping: failed to compile shaders\n");
        return false;
    }

    s_initialized = true;
    return true;
}

void ToneMapping::Shutdown()
{
    if (!s_initialized) return;

    DeleteShaders();
    DestroySceneFBO();
    s_initialized = false;
}

bool ToneMapping::CreateSceneFBO(int width, int height)
{
    if (s_sceneFbo != 0 && s_fboWidth == width && s_fboHeight == height)
        return true;  // Already correct size

    DestroySceneFBO();

    // Create FBO
    glGenFramebuffers(1, &s_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_sceneFbo);

    // Create color texture
    glGenTextures(1, &s_sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, s_sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_sceneColorTex, 0);

    // Create depth renderbuffer
    GLuint depthRbo;
    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        OutputDebugStringA("ToneMapping: FBO creation failed\n");
        DestroySceneFBO();
        return false;
    }

    s_fboWidth = width;
    s_fboHeight = height;
    return true;
}

void ToneMapping::DestroySceneFBO()
{
    if (s_sceneFbo != 0)
    {
        glDeleteFramebuffers(1, &s_sceneFbo);
        s_sceneFbo = 0;
    }
    if (s_sceneColorTex != 0)
    {
        glDeleteTextures(1, &s_sceneColorTex);
        s_sceneColorTex = 0;
    }
    s_fboWidth = 0;
    s_fboHeight = 0;
}

bool ToneMapping::Apply(unsigned int sceneColorTex)
{
    if (!s_initialized || !s_enabled) return true;

    // If Bloom didn't provide a texture, capture the current framebuffer
    if (sceneColorTex == 0)
    {
        // Get current viewport dimensions
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        int width = viewport[2];
        int height = viewport[3];

        // Create/recreate FBO if needed
        if (!CreateSceneFBO(width, height))
            return false;

        // Copy current framebuffer to our texture
        glBindTexture(GL_TEXTURE_2D, s_sceneColorTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width, height);
        sceneColorTex = s_sceneColorTex;
    }

    // Bind tone mapping shader
    glUseProgram(s_toneMappingProgram);

    // Set up texture sampling
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    GLint locScene = glGetUniformLocation(s_toneMappingProgram, "uScene");
    glUniform1i(locScene, 0);

    // Set uniforms
    GLint locStrength = glGetUniformLocation(s_toneMappingProgram, "uStrength");
    glUniform1f(locStrength, s_strength);

    GLint locGamma = glGetUniformLocation(s_toneMappingProgram, "uGamma");
    glUniform1f(locGamma, s_gamma);

    GLint locExposure = glGetUniformLocation(s_toneMappingProgram, "uExposure");
    glUniform1f(locExposure, s_exposure);

    GLint locTint = glGetUniformLocation(s_toneMappingProgram, "uColorTint");
    glUniform3f(locTint, s_tintR, s_tintG, s_tintB);

    GLint locContrast = glGetUniformLocation(s_toneMappingProgram, "uContrast");
    glUniform1f(locContrast, s_contrast);

    // Bind default framebuffer and render fullscreen quad
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);

    RenderFullscreenQuad();

    // Restore state
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);

    return true;
}

// ─── Tone Mapping Presets (20 different moods) ──────────────────────────────

void ToneMapping::PresetOff()                 { s_currentPreset = 0;  s_exposure = 1.0f;  s_tintR = 1.0f;  s_tintG = 1.0f;  s_tintB = 1.0f;  s_contrast = 1.0f; }
void ToneMapping::PresetSubtleDark()          { s_currentPreset = 1;  s_exposure = 0.85f; s_tintR = 0.98f; s_tintG = 0.98f; s_tintB = 1.0f;  s_contrast = 1.1f; }
void ToneMapping::PresetDarkCinema()          { s_currentPreset = 2;  s_exposure = 0.80f; s_tintR = 0.95f; s_tintG = 0.95f; s_tintB = 1.0f;  s_contrast = 1.15f; }
void ToneMapping::PresetMoody()               { s_currentPreset = 3;  s_exposure = 0.75f; s_tintR = 0.92f; s_tintG = 0.95f; s_tintB = 1.05f; s_contrast = 1.2f; }
void ToneMapping::PresetNoir()                { s_currentPreset = 4;  s_exposure = 0.70f; s_tintR = 0.90f; s_tintG = 0.90f; s_tintB = 0.90f; s_contrast = 1.25f; }
void ToneMapping::PresetWarmSepia()           { s_currentPreset = 5;  s_exposure = 0.85f; s_tintR = 1.15f; s_tintG = 1.0f;  s_tintB = 0.8f;  s_contrast = 1.1f; }
void ToneMapping::PresetCoolMystery()         { s_currentPreset = 6;  s_exposure = 0.80f; s_tintR = 0.92f; s_tintG = 0.96f; s_tintB = 1.1f;  s_contrast = 1.1f; }
void ToneMapping::PresetDesaturated()         { s_currentPreset = 7;  s_exposure = 0.85f; s_tintR = 0.92f; s_tintG = 0.92f; s_tintB = 0.92f; s_contrast = 1.0f; }
void ToneMapping::PresetHighContrast()        { s_currentPreset = 8;  s_exposure = 0.90f; s_tintR = 1.0f;  s_tintG = 1.0f;  s_tintB = 1.0f;  s_contrast = 1.35f; }
void ToneMapping::PresetBrightVivid()         { s_currentPreset = 9;  s_exposure = 1.1f;  s_tintR = 1.05f; s_tintG = 1.05f; s_tintB = 1.0f;  s_contrast = 0.9f; }
void ToneMapping::PresetHorror()              { s_currentPreset = 10; s_exposure = 0.65f; s_tintR = 0.88f; s_tintG = 0.95f; s_tintB = 1.15f; s_contrast = 1.3f; }
void ToneMapping::PresetSunset()              { s_currentPreset = 11; s_exposure = 0.85f; s_tintR = 1.2f;  s_tintG = 0.95f; s_tintB = 0.75f; s_contrast = 1.1f; }
void ToneMapping::PresetDeepSea()             { s_currentPreset = 12; s_exposure = 0.70f; s_tintR = 0.85f; s_tintG = 0.90f; s_tintB = 1.2f;  s_contrast = 1.2f; }
void ToneMapping::PresetDusty()               { s_currentPreset = 13; s_exposure = 0.80f; s_tintR = 1.05f; s_tintG = 0.98f; s_tintB = 0.92f; s_contrast = 1.15f; }
void ToneMapping::PresetNeon()                { s_currentPreset = 14; s_exposure = 0.95f; s_tintR = 1.0f;  s_tintG = 1.0f;  s_tintB = 1.0f;  s_contrast = 1.2f; }
void ToneMapping::PresetRetro()               { s_currentPreset = 15; s_exposure = 0.80f; s_tintR = 0.95f; s_tintG = 0.90f; s_tintB = 0.95f; s_contrast = 1.25f; }
void ToneMapping::PresetBleachBypass()        { s_currentPreset = 16; s_exposure = 0.88f; s_tintR = 0.98f; s_tintG = 0.98f; s_tintB = 0.98f; s_contrast = 1.15f; }
void ToneMapping::PresetCyberpunk()           { s_currentPreset = 17; s_exposure = 0.85f; s_tintR = 0.90f; s_tintG = 1.05f; s_tintB = 1.1f;  s_contrast = 1.25f; }
void ToneMapping::PresetGoldenHour()          { s_currentPreset = 18; s_exposure = 0.90f; s_tintR = 1.2f;  s_tintG = 1.05f; s_tintB = 0.85f; s_contrast = 1.05f; }
void ToneMapping::PresetTwilight()            { s_currentPreset = 19; s_exposure = 0.75f; s_tintR = 0.95f; s_tintG = 0.93f; s_tintB = 1.08f; s_contrast = 1.15f; }

void ToneMapping::CyclePreset(int direction)
{
    s_currentPreset = (s_currentPreset + direction + 20) % 20;
    switch(s_currentPreset)
    {
        case 0:  PresetOff(); break;
        case 1:  PresetSubtleDark(); break;
        case 2:  PresetDarkCinema(); break;
        case 3:  PresetMoody(); break;
        case 4:  PresetNoir(); break;
        case 5:  PresetWarmSepia(); break;
        case 6:  PresetCoolMystery(); break;
        case 7:  PresetDesaturated(); break;
        case 8:  PresetHighContrast(); break;
        case 9:  PresetBrightVivid(); break;
        case 10: PresetHorror(); break;
        case 11: PresetSunset(); break;
        case 12: PresetDeepSea(); break;
        case 13: PresetDusty(); break;
        case 14: PresetNeon(); break;
        case 15: PresetRetro(); break;
        case 16: PresetBleachBypass(); break;
        case 17: PresetCyberpunk(); break;
        case 18: PresetGoldenHour(); break;
        case 19: PresetTwilight(); break;
    }
}
