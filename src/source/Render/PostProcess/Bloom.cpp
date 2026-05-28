///////////////////////////////////////////////////////////////////////////////
// Bloom.cpp — post-processing bloom / glow pass
//
// Architecture
// ────────────
//  1. BeginCapture()  — bind scene FBO (full-res RGBA8 + depth RBO)
//  2. [game renders 3-D world normally]
//  3. ApplyBloom()
//       a. Threshold pass  : full-res scene → half-res bright texture
//       b. Blur passes (N) : ping-pong Gaussian blur on the half-res texture
//       c. Composite pass  : scene + blurred glow → default framebuffer
//  4. [game renders 2-D UI on top, untouched]
//
// All GLSL is GLSL 1.20 (OpenGL 2.1 compatibility profile) so it coexists
// cleanly with the fixed-function rendering used everywhere else in the game.
// Immediate-mode quads (glBegin/glEnd) are used intentionally — it keeps the
// post-process code self-contained without adding VBOs / VAOs.
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Bloom.h"
#include <cstdio>
#include <cstring>

// ─── static member definitions ──────────────────────────────────────────────

bool  Bloom::s_initialized   = false;
bool  Bloom::s_enabled       = true;
float Bloom::s_threshold     = 0.65f; // pixels above this luminance glow
float Bloom::s_strength      = 1.1f;  // additive composite strength
int   Bloom::s_blurPasses    = 4;     // horizontal+vertical iterations

int   Bloom::s_fboWidth      = 0;
int   Bloom::s_fboHeight     = 0;

unsigned int Bloom::s_sceneFbo      = 0;
unsigned int Bloom::s_sceneColorTex = 0;
unsigned int Bloom::s_sceneDepthRbo = 0;

unsigned int Bloom::s_bloomFbo[2]   = {0, 0};
unsigned int Bloom::s_bloomTex[2]   = {0, 0};

unsigned int Bloom::s_thresholdProg = 0;
unsigned int Bloom::s_blurProg      = 0;
unsigned int Bloom::s_compositeProg = 0;

// ─── GLSL source strings ─────────────────────────────────────────────────────
//
// All shaders use #version 120 (GLSL for OpenGL 2.1, compatible with any
// modern compatibility-profile context).  Built-in attributes gl_Vertex /
// gl_MultiTexCoord0 are populated by the immediate-mode glTexCoord2f /
// glVertex2f calls in RenderFullscreenQuad().

static const char* kVertSrc = R"GLSL(
#version 120
void main()
{
    gl_Position    = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_TexCoord[0] = gl_MultiTexCoord0;
}
)GLSL";

// Pass 1 — extract pixels above the luminance threshold.
// Uses a smooth knee so the transition isn't a harsh cut.
static const char* kThresholdFragSrc = R"GLSL(
#version 120
uniform sampler2D uScene;
uniform float     uThreshold;

void main()
{
    vec3  c          = texture2D(uScene, gl_TexCoord[0].xy).rgb;
    float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // Smooth knee: ramp from 80 % of threshold to 100 %.
    float contrib    = smoothstep(uThreshold * 0.8, uThreshold, brightness);
    gl_FragColor     = vec4(c * contrib, 1.0);
}
)GLSL";

// Pass 2 — separable 9-tap Gaussian blur.
// uHorizontal == 1 → blur along X; 0 → blur along Y.
// uTexelSize carries (1/width, 1/height) of the bloom texture.
static const char* kBlurFragSrc = R"GLSL(
#version 120
uniform sampler2D uImage;
uniform vec2      uTexelSize;
uniform int       uHorizontal;

void main()
{
    // 5-tap Gaussian weights (normalised, sigma ≈ 1.3)
    const float w0 = 0.22702703;
    const float w1 = 0.19459459;
    const float w2 = 0.12162162;
    const float w3 = 0.05405405;
    const float w4 = 0.01621622;

    vec2 uv  = gl_TexCoord[0].xy;
    vec2 dir = (uHorizontal != 0) ? vec2(uTexelSize.x, 0.0)
                                  : vec2(0.0, uTexelSize.y);

    vec3 result =
        texture2D(uImage, uv             ).rgb * w0 +
        texture2D(uImage, uv + dir * 1.0 ).rgb * w1 +
        texture2D(uImage, uv - dir * 1.0 ).rgb * w1 +
        texture2D(uImage, uv + dir * 2.0 ).rgb * w2 +
        texture2D(uImage, uv - dir * 2.0 ).rgb * w2 +
        texture2D(uImage, uv + dir * 3.0 ).rgb * w3 +
        texture2D(uImage, uv - dir * 3.0 ).rgb * w3 +
        texture2D(uImage, uv + dir * 4.0 ).rgb * w4 +
        texture2D(uImage, uv - dir * 4.0 ).rgb * w4;

    gl_FragColor = vec4(result, 1.0);
}
)GLSL";

// Pass 3 — additive composite: original scene + scaled bloom glow.
static const char* kCompositeFragSrc = R"GLSL(
#version 120
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float     uStrength;

void main()
{
    vec2 uv    = gl_TexCoord[0].xy;
    vec3 scene = texture2D(uScene, uv).rgb;
    vec3 bloom = texture2D(uBloom, uv).rgb;
    gl_FragColor = vec4(scene + bloom * uStrength, 1.0);
}
)GLSL";

// ─── shader helpers ───────────────────────────────────────────────────────────

unsigned int Bloom::CompileShader(unsigned int type, const char* src)
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
        sprintf_s(msg, sizeof(msg), "Bloom: shader compile error:\n%s\n", log);
        OutputDebugStringA(msg);
        glDeleteShader(s);
        return 0;
    }
    return static_cast<unsigned int>(s);
}

unsigned int Bloom::LinkProgram(unsigned int vert, unsigned int frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, static_cast<GLuint>(vert));
    glAttachShader(prog, static_cast<GLuint>(frag));
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        char msg[600];
        sprintf_s(msg, sizeof(msg), "Bloom: program link error:\n%s\n", log);
        OutputDebugStringA(msg);
        glDeleteProgram(prog);
        return 0;
    }
    return static_cast<unsigned int>(prog);
}

bool Bloom::CompileShaders()
{
    // Compile one vertex shader per program.  A single compiled GLSL shader
    // object can technically be shared, but the extra compile cost is trivial
    // (happens once at startup) and keeping them separate avoids any driver quirks.
    unsigned int vT = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    unsigned int vB = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    unsigned int vC = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    unsigned int fT = CompileShader(GL_FRAGMENT_SHADER, kThresholdFragSrc);
    unsigned int fB = CompileShader(GL_FRAGMENT_SHADER, kBlurFragSrc);
    unsigned int fC = CompileShader(GL_FRAGMENT_SHADER, kCompositeFragSrc);

    bool allOk = vT && vB && vC && fT && fB && fC;

    if (allOk)
    {
        s_thresholdProg = LinkProgram(vT, fT);
        s_blurProg      = LinkProgram(vB, fB);
        s_compositeProg = LinkProgram(vC, fC);
        allOk = s_thresholdProg && s_blurProg && s_compositeProg;
    }

    // Shader objects no longer needed once linked into programs
    if (vT) glDeleteShader(vT);
    if (vB) glDeleteShader(vB);
    if (vC) glDeleteShader(vC);
    if (fT) glDeleteShader(fT);
    if (fB) glDeleteShader(fB);
    if (fC) glDeleteShader(fC);

    if (!allOk)
    {
        DeleteShaders();
        return false;
    }

    // Bind sampler uniforms once (they never change)
    glUseProgram(s_thresholdProg);
    glUniform1i(glGetUniformLocation(s_thresholdProg, "uScene"), 0);

    glUseProgram(s_blurProg);
    glUniform1i(glGetUniformLocation(s_blurProg, "uImage"), 0);

    glUseProgram(s_compositeProg);
    glUniform1i(glGetUniformLocation(s_compositeProg, "uScene"), 0);
    glUniform1i(glGetUniformLocation(s_compositeProg, "uBloom"), 1);

    glUseProgram(0);
    return true;
}

void Bloom::DeleteShaders()
{
    if (s_thresholdProg) { glDeleteProgram(s_thresholdProg); s_thresholdProg = 0; }
    if (s_blurProg)      { glDeleteProgram(s_blurProg);      s_blurProg      = 0; }
    if (s_compositeProg) { glDeleteProgram(s_compositeProg); s_compositeProg = 0; }
}

// ─── FBO management ───────────────────────────────────────────────────────────

bool Bloom::CreateFBOs(int width, int height)
{
    DestroyFBOs();

    if (width <= 0 || height <= 0)
        return false;

    s_fboWidth  = width;
    s_fboHeight = height;

    // Half-resolution for the bloom buffers (avoids precision aliasing at full res
    // and cuts bandwidth roughly in half for the blur passes).
    const int hw = width  / 2 + (width  & 1);
    const int hh = height / 2 + (height & 1);

    // ── Scene FBO (full resolution, RGBA8 colour + 16-bit depth RBO) ─────────
    glGenFramebuffers(1, &s_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_sceneFbo);

    glGenTextures(1, &s_sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, s_sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_sceneColorTex, 0);

    glGenRenderbuffers(1, &s_sceneDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_sceneDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, s_sceneDepthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        OutputDebugStringA("Bloom: scene FBO incomplete — bloom disabled.\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        DestroyFBOs();
        return false;
    }

    // ── Bloom ping-pong FBOs (half resolution, RGB8) ──────────────────────────
    glGenFramebuffers(2, s_bloomFbo);
    glGenTextures(2, s_bloomTex);

    for (int i = 0; i < 2; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[i]);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, hw, hh, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, s_bloomTex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            OutputDebugStringA("Bloom: bloom FBO incomplete — bloom disabled.\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DestroyFBOs();
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    OutputDebugStringA("Bloom: FBOs created OK.\n");
    return true;
}

void Bloom::DestroyFBOs()
{
    if (s_sceneFbo)
    {
        glDeleteFramebuffers(1, &s_sceneFbo);
        s_sceneFbo = 0;
    }
    if (s_sceneColorTex)
    {
        glDeleteTextures(1, &s_sceneColorTex);
        s_sceneColorTex = 0;
    }
    if (s_sceneDepthRbo)
    {
        glDeleteRenderbuffers(1, &s_sceneDepthRbo);
        s_sceneDepthRbo = 0;
    }
    if (s_bloomFbo[0] || s_bloomFbo[1])
    {
        glDeleteFramebuffers(2, s_bloomFbo);
        s_bloomFbo[0] = s_bloomFbo[1] = 0;
    }
    if (s_bloomTex[0] || s_bloomTex[1])
    {
        glDeleteTextures(2, s_bloomTex);
        s_bloomTex[0] = s_bloomTex[1] = 0;
    }
    s_fboWidth = s_fboHeight = 0;
}

// ─── fullscreen quad ─────────────────────────────────────────────────────────
//
// Sets up an orthographic projection in [-1,1] clip space, draws two triangles
// that fill the viewport, then restores the matrix stack.  The compatibility-
// profile vertex shader receives position via gl_Vertex and UV via
// gl_MultiTexCoord0 — both are populated by glTexCoord2f / glVertex2f.

void Bloom::RenderFullscreenQuad()
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);  glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f);  glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f);  glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f);  glVertex2f(-1.0f,  1.0f);
    glEnd();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ─── public API ───────────────────────────────────────────────────────────────

bool Bloom::Initialize()
{
    if (s_initialized)
        return true;

    // Require OpenGL 2.0 (GLSL shaders) and ARB_framebuffer_object / OpenGL 3.0.
    // On any GPU from the last ~15 years with a compatibility context these are
    // guaranteed, but we check to give a clean error message on old hardware.
    if (!GLEW_VERSION_2_0)
    {
        OutputDebugStringA("Bloom: OpenGL 2.0 unavailable — bloom disabled.\n");
        return false;
    }
    if (!GLEW_ARB_framebuffer_object && !GLEW_VERSION_3_0)
    {
        OutputDebugStringA("Bloom: GL_ARB_framebuffer_object unavailable — bloom disabled.\n");
        return false;
    }

    if (!CompileShaders())
    {
        OutputDebugStringA("Bloom: shader compilation failed — bloom disabled.\n");
        return false;
    }

    s_initialized = true;
    OutputDebugStringA("Bloom: initialized successfully.\n");
    return true;
}

void Bloom::Shutdown()
{
    DestroyFBOs();
    DeleteShaders();
    s_initialized = false;
}

// ─── per-frame ────────────────────────────────────────────────────────────────

// WindowWidth / WindowHeight are extern globals from ZzzOpenglUtil.cpp.
extern unsigned int WindowWidth;
extern unsigned int WindowHeight;

// CachTexture is the texture-binding cache in ZzzOpenglUtil.cpp.
// We invalidate it after the post-process passes so the next BindTexture()
// call correctly rebinds instead of assuming the cache is still valid.
extern int CachTexture;

void Bloom::BeginCapture()
{
    if (!s_initialized || !s_enabled)
        return;

    const int w = static_cast<int>(WindowWidth);
    const int h = static_cast<int>(WindowHeight);
    if (w <= 0 || h <= 0)
        return;

    // Lazily create / recreate FBOs when the window size changes.
    if (w != s_fboWidth || h != s_fboHeight)
    {
        if (!CreateFBOs(w, h))
        {
            // Permanently disable if the driver can't create FBOs
            // (prevents spamming error logs every frame).
            s_initialized = false;
            return;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, s_sceneFbo);

    // SetWorldClearColor() already called glClear on the *default* framebuffer
    // before we got here.  We must also clear our FBO so the scene doesn't
    // render on top of leftover data from the previous frame.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Bloom::ApplyBloom()
{
    if (!s_initialized || !s_enabled || !s_sceneFbo)
        return;

    // Unbind scene FBO — the 3-D render is now in s_sceneColorTex.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const int w  = s_fboWidth;
    const int h  = s_fboHeight;
    const int hw = w / 2 + (w & 1);
    const int hh = h / 2 + (h & 1);

    // ── Save GL state affected by the post-process passes ────────────────────
    const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend     = glIsEnabled(GL_BLEND);
    const GLboolean cullFace  = glIsEnabled(GL_CULL_FACE);
    const GLboolean alphaTest = glIsEnabled(GL_ALPHA_TEST);
    const GLboolean fog       = glIsEnabled(GL_FOG);
    const GLboolean tex2d     = glIsEnabled(GL_TEXTURE_2D);
    GLint prevActiveUnit      = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveUnit);
    GLint prevDepthMask       = GL_TRUE;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    // ── Set up clean state for 2-D fullscreen passes ─────────────────────────
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);

    // ── Pass 1: Brightness threshold (full-res → half-res bloomFbo[0]) ───────
    glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[0]);
    glViewport(0, 0, hw, hh);

    glUseProgram(s_thresholdProg);
    glUniform1f(glGetUniformLocation(s_thresholdProg, "uThreshold"), s_threshold);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_sceneColorTex);

    RenderFullscreenQuad();

    // ── Pass 2: Separable Gaussian blur, N iterations ─────────────────────────
    glUseProgram(s_blurProg);
    const GLint locTexelSize  = glGetUniformLocation(s_blurProg, "uTexelSize");
    const GLint locHorizontal = glGetUniformLocation(s_blurProg, "uHorizontal");
    glUniform2f(locTexelSize, 1.0f / static_cast<float>(hw),
                              1.0f / static_cast<float>(hh));

    for (int i = 0; i < s_blurPasses; ++i)
    {
        // Horizontal pass: bloomFbo[0] → bloomFbo[1]
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[1]);
        glViewport(0, 0, hw, hh);
        glUniform1i(locHorizontal, 1);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[0]);
        RenderFullscreenQuad();

        // Vertical pass: bloomFbo[1] → bloomFbo[0]
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[0]);
        glViewport(0, 0, hw, hh);
        glUniform1i(locHorizontal, 0);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[1]);
        RenderFullscreenQuad();
    }
    // bloomTex[0] now contains the final blurred glow.

    // ── Pass 3: Composite — scene + bloom → default framebuffer ─────────────
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);

    glUseProgram(s_compositeProg);
    glUniform1f(glGetUniformLocation(s_compositeProg, "uStrength"), s_strength);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_sceneColorTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_bloomTex[0]);

    RenderFullscreenQuad();

    // ── Restore GL state ─────────────────────────────────────────────────────
    glUseProgram(0);

    // Unbind our textures from both units before handing control back
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore the active texture unit the game was using
    glActiveTexture(static_cast<GLenum>(prevActiveUnit));

    // Invalidate the game's texture-binding cache so the next BindTexture()
    // call does a real glBindTexture instead of silently skipping it.
    CachTexture = ~0;

    if (depthTest) glEnable(GL_DEPTH_TEST);  else glDisable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (cullFace)  glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
    if (alphaTest) glEnable(GL_ALPHA_TEST);   else glDisable(GL_ALPHA_TEST);
    if (fog)       glEnable(GL_FOG);          else glDisable(GL_FOG);
    if (!tex2d)    glDisable(GL_TEXTURE_2D);
    glDepthMask(static_cast<GLboolean>(prevDepthMask));
}
