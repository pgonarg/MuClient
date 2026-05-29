///////////////////////////////////////////////////////////////////////////////
// FXAAManager.cpp — Fast Approximate Anti-Aliasing post-process
//
// Applies FXAA to smooth jagged edges in the final rendered image.
// Architecture:
//   1. Apply() receives input texture from previous post-process (Bloom/ToneMapping)
//   2. Renders to temporary FBO with FXAA shader
//   3. Output composited to default framebuffer
//
// All GLSL is GLSL 1.20 (OpenGL 2.1 compatibility) for consistency.
// Uses immediate-mode quad rendering to keep the code self-contained.
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FXAAManager.h"
#include "Render/Shaders/FXAAShader.glsl.h"
#include <GL/glew.h>
#include <cstdio>
#include <cstring>

// ─── static member definitions ──────────────────────────────────────────────

bool  FXAAManager::s_initialized     = false;
bool  FXAAManager::s_enabled         = true;
float FXAAManager::s_blendStrength   = 1.0f;
const char* FXAAManager::s_lastError = "Not initialized";

int   FXAAManager::s_fboWidth        = 0;
int   FXAAManager::s_fboHeight       = 0;

unsigned int FXAAManager::s_fxaaFbo        = 0;
unsigned int FXAAManager::s_inputTex       = 0;
unsigned int FXAAManager::s_outputTex      = 0;
unsigned int FXAAManager::s_fxaaProgram    = 0;
int   FXAAManager::s_uTextureLocation      = -1;
int   FXAAManager::s_uRcpFrameLocation     = -1;

// ─── GLSL source strings ──────────────────────────────────────────────────────

static const char* FXAA_VERT_SRC = R"GLSL(
#version 120
void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)GLSL";

// FXAA fragment shader - edge detection and smoothing
static const char* FXAA_FRAG_SRC = R"GLSL(
#version 120

uniform sampler2D uTexture;
uniform vec2      uRcpFrame;

void main()
{
    vec2 posM = gl_TexCoord[0].xy;

    // Sample center and 4 neighbors
    vec3 rgbNW = texture2D(uTexture, posM + vec2(-1.0, -1.0) * uRcpFrame).xyz;
    vec3 rgbNE = texture2D(uTexture, posM + vec2( 1.0, -1.0) * uRcpFrame).xyz;
    vec3 rgbSW = texture2D(uTexture, posM + vec2(-1.0,  1.0) * uRcpFrame).xyz;
    vec3 rgbSE = texture2D(uTexture, posM + vec2( 1.0,  1.0) * uRcpFrame).xyz;
    vec3 rgbM  = texture2D(uTexture, posM                      ).xyz;

    // Luminance calculation (perceived brightness)
    const vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    // Find local min/max luminance
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Early exit: no edge if contrast is below threshold
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0833, lumaMax * 0.25))
    {
        gl_FragColor = vec4(rgbM, 1.0);
        return;
    }

    // Detect edge direction by comparing diagonal pairs
    float lumaL = (lumaNW + lumaSW) * 0.5;  // left
    float lumaR = (lumaNE + lumaSE) * 0.5;  // right
    float lumaU = (lumaNW + lumaNE) * 0.5;  // up
    float lumaD = (lumaSW + lumaSE) * 0.5;  // down

    float lumaHor = abs(lumaL - lumaR);     // horizontal edge strength
    float lumaVer = abs(lumaU - lumaD);     // vertical edge strength

    // Determine edge orientation
    bool isHorizontal = lumaHor > lumaVer;

    // Measure gradient along edge
    float luma1 = isHorizontal ? lumaSW : lumaNW;
    float luma2 = isHorizontal ? lumaSE : lumaNE;

    // Search direction (how far to blend along the edge)
    float searchLength = 2.0;
    if (abs(luma1 - lumaM) < abs(luma2 - lumaM))
    {
        searchLength = -searchLength;
    }

    // Direction vectors for edge-aligned blending
    vec2 blendDir = isHorizontal ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
    vec2 p1 = posM + blendDir * searchLength * uRcpFrame;
    vec2 p2 = posM - blendDir * searchLength * uRcpFrame;

    // Sample along edge direction
    vec3 rgb1 = texture2D(uTexture, p1).xyz;
    vec3 rgb2 = texture2D(uTexture, p2).xyz;
    float luma1s = dot(rgb1, luma);
    float luma2s = dot(rgb2, luma);

    // Determine blend weight
    float weight = 0.5;
    if (abs(luma1s - lumaM) < abs(luma2s - lumaM))
    {
        weight = 0.25;
    }
    else
    {
        weight = 0.75;
    }

    // Apply FXAA: blend edge sample with center
    vec3 finalRGB = mix(rgbM, mix(rgb1, rgb2, weight), 0.25);

    gl_FragColor = vec4(finalRGB, 1.0);
}
)GLSL";

// ─── Implementation ─────────────────────────────────────────────────────────

bool FXAAManager::Initialize()
{
    if (s_initialized)
        return true;

    s_lastError = "Unknown error";

    // Check required extensions
    if (!GLEW_EXT_framebuffer_object)
    {
        s_lastError = "FXAA: GL_EXT_framebuffer_object not supported";
        OutputDebugStringA("FXAA: missing FBO support\n");
        return false;
    }

    // Compile shaders
    if (!CompileShaders())
    {
        OutputDebugStringA("FXAA: shader compilation failed\n");
        return false;
    }

    s_initialized = true;
    s_lastError = "OK";
    OutputDebugStringA("FXAA: initialized successfully\n");
    return true;
}

void FXAAManager::Shutdown()
{
    if (!s_initialized)
        return;

    DeleteShaders();
    DestroyFBO();

    s_initialized = false;
    s_lastError = "Shut down";
}

bool FXAAManager::CompileShaders()
{
    GLuint vertShader = CompileShader(GL_VERTEX_SHADER, FXAA_VERT_SRC);
    if (!vertShader)
        return false;

    GLuint fragShader = CompileShader(GL_FRAGMENT_SHADER, FXAA_FRAG_SRC);
    if (!fragShader)
    {
        glDeleteShader(vertShader);
        return false;
    }

    s_fxaaProgram = LinkProgram(vertShader, fragShader);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    if (!s_fxaaProgram)
        return false;

    // Cache uniform locations
    s_uTextureLocation = glGetUniformLocation(s_fxaaProgram, "uTexture");
    s_uRcpFrameLocation = glGetUniformLocation(s_fxaaProgram, "uRcpFrame");

    if (s_uTextureLocation < 0 || s_uRcpFrameLocation < 0)
    {
        s_lastError = "FXAA: failed to get uniform locations";
        glDeleteProgram(s_fxaaProgram);
        s_fxaaProgram = 0;
        return false;
    }

    return true;
}

unsigned int FXAAManager::CompileShader(unsigned int type, const char* src)
{
    GLuint shader = glCreateShader(static_cast<GLenum>(type));
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        char msg[600];
        sprintf_s(msg, sizeof(msg), "FXAA shader compile error:\n%s\n", log);
        OutputDebugStringA(msg);
        s_lastError = "Shader compilation failed";
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

unsigned int FXAAManager::LinkProgram(unsigned int vert, unsigned int frag)
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
        sprintf_s(msg, sizeof(msg), "FXAA program link error:\n%s\n", log);
        OutputDebugStringA(msg);
        s_lastError = "Program linking failed";
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

void FXAAManager::DeleteShaders()
{
    if (s_fxaaProgram)
    {
        glDeleteProgram(s_fxaaProgram);
        s_fxaaProgram = 0;
    }
}

bool FXAAManager::CreateFBO(int width, int height)
{
    if (s_fboWidth == width && s_fboHeight == height && s_inputTex)
        return true;

    if (width <= 0 || height <= 0)
        return false;

    // Allocate input texture if needed (for backbuffer copy)
    if (!s_inputTex)
    {
        glGenTextures(1, &s_inputTex);
        glBindTexture(GL_TEXTURE_2D, s_inputTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    s_fboWidth = width;
    s_fboHeight = height;
    return true;
}

void FXAAManager::DestroyFBO()
{
    if (s_inputTex)
    {
        glDeleteTextures(1, &s_inputTex);
        s_inputTex = 0;
    }

    s_fboWidth = 0;
    s_fboHeight = 0;
}

void FXAAManager::RenderFullscreenQuad()
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
    {
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 1.0f);
    }
    glEnd();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void FXAAManager::Apply(unsigned int inputTex)
{
    if (!s_initialized || !s_enabled)
        return;

    // Get current viewport
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int width = viewport[2];
    int height = viewport[3];

    if (width <= 0 || height <= 0)
        return;

    // Ensure texture buffer is allocated
    if (!CreateFBO(width, height))
        return;

    // If no input texture provided, read from current framebuffer
    unsigned int sourceTexture = inputTex;
    if (sourceTexture == 0)
    {
        // Ensure we're reading from the default framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // Copy current backbuffer to texture
        glBindTexture(GL_TEXTURE_2D, s_inputTex);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewport[0], viewport[1], width, height, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        sourceTexture = s_inputTex;
    }

    // Save GL state
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    // Apply FXAA to fullscreen quad
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);

    glUseProgram(s_fxaaProgram);

    // Set uniforms
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(s_uTextureLocation, 0);

    // Reciprocal of resolution for pixel-space offsets
    float rcpFrameX = 1.0f / width;
    float rcpFrameY = 1.0f / height;
    glUniform2f(s_uRcpFrameLocation, rcpFrameX, rcpFrameY);

    // Render fullscreen quad with FXAA applied (output goes to screen)
    RenderFullscreenQuad();

    // Unbind
    glUseProgram(0);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    // Restore GL state
    glPopAttrib();
}
