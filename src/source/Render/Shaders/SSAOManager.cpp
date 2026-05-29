///////////////////////////////////////////////////////////////////////////////
// SSAOManager.cpp - Screen Space Ambient Occlusion implementation
//
// Manages G-Buffer capture, SSAO computation, bilateral blur, and composite
// into the final scene. Integrates with Bloom post-processing pipeline.
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SSAOManager.h"
#include "SSAOShader.glsl.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace SEASON3B {

// Global instance
SSAOManager* g_SSAOManager = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

SSAOManager::SSAOManager()
    : m_bInitialized(false),
      m_screenWidth(0),
      m_screenHeight(0),
      m_gBufferProgram(0),
      m_ssaoComputeProgram(0),
      m_blurProgram(0),
      m_compositeProgram(0),
      m_gBufferFBO(0),
      m_gBufferColorTex(0),
      m_gBufferDepthRBO(0),
      m_aoResultFBO(0),
      m_aoResultTex(0),
      m_noiseTexture(0)
{
    m_settings.radius = 0.8f;           // Slightly smaller sampling radius for subtlety
    m_settings.bias = 0.025f;
    m_settings.intensity = 0.3f;        // Reduced from 1.0 to 0.3 (30% darkening - subtle)
    m_settings.sampleCount = 8;
    m_settings.blurRadius = 3.0f;       // Slightly reduced blur
    m_settings.enabled = true;          // SSAO enabled by default

    m_ssaoFBO[0] = m_ssaoFBO[1] = 0;
    m_ssaoTex[0] = m_ssaoTex[1] = 0;
}

SSAOManager::~SSAOManager()
{
    if (m_bInitialized) {
        Shutdown();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool SSAOManager::Initialize()
{
    if (m_bInitialized) {
        LogError("SSAOManager already initialized");
        return false;
    }

    // Compile all shader programs
    if (!CompileShaders()) {
        LogError("Failed to compile SSAO shaders");
        return false;
    }

    // Create noise texture for sample randomization
    if (!CreateNoiseTexture()) {
        LogError("Failed to create noise texture");
        Cleanup();
        return false;
    }

    // Create G-Buffer and intermediate FBOs (will be resized in BeginGBufferCapture)
    if (!CreateFramebuffers()) {
        LogError("Failed to create framebuffers");
        Cleanup();
        return false;
    }

    m_bInitialized = true;
    return true;
}

void SSAOManager::Shutdown()
{
    if (!m_bInitialized) return;

    Cleanup();
    m_bInitialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer Capture Pipeline
// ─────────────────────────────────────────────────────────────────────────────

void SSAOManager::BeginGBufferCapture(int width, int height)
{
    if (!m_bInitialized || !m_settings.enabled) return;

    // Resize framebuffers if screen size changed
    if (m_screenWidth != width || m_screenHeight != height) {
        m_screenWidth = width;
        m_screenHeight = height;

        if (m_gBufferFBO != 0) {
            glDeleteFramebuffers(1, &m_gBufferFBO);
            glDeleteTextures(1, &m_gBufferColorTex);
            glDeleteRenderbuffers(1, &m_gBufferDepthRBO);

            for (int i = 0; i < 2; ++i) {
                if (m_ssaoFBO[i] != 0) glDeleteFramebuffers(1, &m_ssaoFBO[i]);
                if (m_ssaoTex[i] != 0) glDeleteTextures(1, &m_ssaoTex[i]);
            }
            if (m_aoResultFBO != 0) glDeleteFramebuffers(1, &m_aoResultFBO);
            if (m_aoResultTex != 0) glDeleteTextures(1, &m_aoResultTex);

            CreateFramebuffers();
        }
    }

    // Bind G-Buffer FBO and clear
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    // Set up camera / projection (inherited from game state)
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
}

void SSAOManager::EndGBufferCapture()
{
    if (!m_bInitialized || !m_settings.enabled) return;

    // Restore framebuffer and matrices
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply SSAO: compute, blur, and composite
// ─────────────────────────────────────────────────────────────────────────────

bool SSAOManager::ApplySSAO(GLuint sceneColorTexture)
{
    if (!m_bInitialized || !m_settings.enabled) return true;

    // Safety check: if G-Buffer wasn't captured properly, skip SSAO for this frame
    if (m_gBufferColorTex == 0 || sceneColorTexture == 0)
    {
        // No G-Buffer data available; SSAO is skipped this frame (scene already rendered)
        return true;  // Not an error, just a no-op this frame
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        char buf[256];
        snprintf(buf, sizeof(buf), "GL error before SSAO: 0x%x", err);
        LogError(buf);
    }

    // ---- Pass 1: SSAO Computation ----
    // Compute occlusion from G-Buffer data
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO[0]);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_ssaoComputeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gBufferColorTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);

    SetupSSAOComputeUniforms();
    RenderFullscreenQuad();
    glUseProgram(0);

    // ---- Pass 2: Bilateral Blur ----
    // Smooth AO result while preserving depth discontinuities
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO[1]);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_blurProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoTex[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gBufferColorTex);

    SetupBlurUniforms();
    RenderFullscreenQuad();
    glUseProgram(0);

    // ---- Pass 3: Composite (apply AO to scene) ----
    // Multiply scene color by AO factor (darken occluded areas)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    glUseProgram(m_compositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_ssaoTex[1]);

    SetupCompositeUniforms(sceneColorTexture);
    RenderFullscreenQuad();
    glUseProgram(0);

    // Restore state
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    err = glGetError();
    if (err != GL_NO_ERROR) {
        char buf[256];
        snprintf(buf, sizeof(buf), "GL error after SSAO: 0x%x", err);
        LogError(buf);
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper Methods: Shader Compilation, FBO Creation, Uniforms
// ─────────────────────────────────────────────────────────────────────────────

bool SSAOManager::CompileShaders()
{
    // Helper lambda to compile a shader program
    auto compileProgram = [this](const char* vertSrc, const char* fragSrc) -> GLuint {
        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vertSrc, nullptr);
        glCompileShader(vert);

        GLint compiled;
        glGetShaderiv(vert, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[1024];
            glGetShaderInfoLog(vert, sizeof(log), nullptr, log);
            LogError(std::string("Vertex shader compile error: ") + log);
            glDeleteShader(vert);
            return 0;
        }

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fragSrc, nullptr);
        glCompileShader(frag);

        glGetShaderiv(frag, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[1024];
            glGetShaderInfoLog(frag, sizeof(log), nullptr, log);
            LogError(std::string("Fragment shader compile error: ") + log);
            glDeleteShader(vert);
            glDeleteShader(frag);
            return 0;
        }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert);
        glAttachShader(prog, frag);
        glLinkProgram(prog);

        GLint linked;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            LogError(std::string("Program link error: ") + log);
            glDeleteProgram(prog);
            glDeleteShader(vert);
            glDeleteShader(frag);
            return 0;
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
        return prog;
    };

    // Compile all four shader programs
    m_gBufferProgram = compileProgram(
        Shaders::SSAO_GBUFFER_VERTEX_SHADER,
        Shaders::SSAO_GBUFFER_FRAGMENT_SHADER
    );
    if (!m_gBufferProgram) return false;

    m_ssaoComputeProgram = compileProgram(
        Shaders::SSAO_COMPUTE_VERTEX_SHADER,
        Shaders::SSAO_COMPUTE_FRAGMENT_SHADER
    );
    if (!m_ssaoComputeProgram) return false;

    m_blurProgram = compileProgram(
        Shaders::SSAO_BLUR_VERTEX_SHADER,
        Shaders::SSAO_BLUR_FRAGMENT_SHADER
    );
    if (!m_blurProgram) return false;

    m_compositeProgram = compileProgram(
        Shaders::SSAO_COMPOSITE_VERTEX_SHADER,
        Shaders::SSAO_COMPOSITE_FRAGMENT_SHADER
    );
    if (!m_compositeProgram) return false;

    return true;
}

bool SSAOManager::CreateFramebuffers()
{
    if (m_screenWidth <= 0 || m_screenHeight <= 0) {
        LogError("Invalid screen dimensions");
        return false;
    }

    // Create G-Buffer FBO (will be bound and rendered into by game)
    glGenFramebuffers(1, &m_gBufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);

    glGenTextures(1, &m_gBufferColorTex);
    glBindTexture(GL_TEXTURE_2D, m_gBufferColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_screenWidth, m_screenHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gBufferColorTex, 0);

    glGenRenderbuffers(1, &m_gBufferDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_screenWidth, m_screenHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gBufferDepthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LogError("G-Buffer FBO incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Create SSAO ping-pong FBOs (for SSAO compute + blur)
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_ssaoFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO[i]);

        glGenTextures(1, &m_ssaoTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_ssaoTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_screenWidth, m_screenHeight, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoTex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LogError("SSAO ping-pong FBO incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool SSAOManager::CreateNoiseTexture()
{
    // Generate random 4x4 texture with random vectors
    unsigned char noise[NOISE_SIZE * NOISE_SIZE * 3];
    for (int i = 0; i < NOISE_SIZE * NOISE_SIZE * 3; ++i) {
        noise[i] = rand() % 256;
    }

    glGenTextures(1, &m_noiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, NOISE_SIZE, NOISE_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, noise);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return true;
}

void SSAOManager::RenderFullscreenQuad()
{
    // Render a full-screen quad using immediate mode
    // This matches the Bloom post-process pattern
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glBegin(GL_QUADS);
    {
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    }
    glEnd();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void SSAOManager::SetupSSAOComputeUniforms()
{
    GLint locGBuffer = glGetUniformLocation(m_ssaoComputeProgram, "uGBufferPos");
    GLint locNoise = glGetUniformLocation(m_ssaoComputeProgram, "uNoise");
    GLint locNoiseScale = glGetUniformLocation(m_ssaoComputeProgram, "uNoiseScale");
    GLint locRadius = glGetUniformLocation(m_ssaoComputeProgram, "uRadius");
    GLint locBias = glGetUniformLocation(m_ssaoComputeProgram, "uBias");
    GLint locSampleCount = glGetUniformLocation(m_ssaoComputeProgram, "uSampleCount");
    GLint locIntensity = glGetUniformLocation(m_ssaoComputeProgram, "uIntensity");

    glUniform1i(locGBuffer, 0);
    glUniform1i(locNoise, 1);

    float noiseScaleX = (float)m_screenWidth / NOISE_SIZE;
    float noiseScaleY = (float)m_screenHeight / NOISE_SIZE;
    glUniform2f(locNoiseScale, noiseScaleX, noiseScaleY);

    glUniform1f(locRadius, m_settings.radius);
    glUniform1f(locBias, m_settings.bias);
    glUniform1i(locSampleCount, m_settings.sampleCount);
    glUniform1f(locIntensity, m_settings.intensity);
}

void SSAOManager::SetupBlurUniforms()
{
    GLint locAO = glGetUniformLocation(m_blurProgram, "uAO");
    GLint locGBuffer = glGetUniformLocation(m_blurProgram, "uGBufferPos");
    GLint locTexelSize = glGetUniformLocation(m_blurProgram, "uTexelSize");
    GLint locBlurRadius = glGetUniformLocation(m_blurProgram, "uBlurRadius");

    glUniform1i(locAO, 0);
    glUniform1i(locGBuffer, 1);

    float texelX = 1.0f / m_screenWidth;
    float texelY = 1.0f / m_screenHeight;
    glUniform2f(locTexelSize, texelX, texelY);

    glUniform1f(locBlurRadius, m_settings.blurRadius);
}

void SSAOManager::SetupCompositeUniforms(GLuint sceneColorTex)
{
    GLint locScene = glGetUniformLocation(m_compositeProgram, "uScene");
    GLint locAO = glGetUniformLocation(m_compositeProgram, "uAO");

    glUniform1i(locScene, 0);
    glUniform1i(locAO, 1);
}

void SSAOManager::LogError(const std::string& msg)
{
    m_lastError = msg;
    // Log to stderr for now (can be integrated with proper logging later)
    fprintf(stderr, "[SSAO] %s\n", msg.c_str());
}

void SSAOManager::Cleanup()
{
    if (m_gBufferProgram) glDeleteProgram(m_gBufferProgram);
    if (m_ssaoComputeProgram) glDeleteProgram(m_ssaoComputeProgram);
    if (m_blurProgram) glDeleteProgram(m_blurProgram);
    if (m_compositeProgram) glDeleteProgram(m_compositeProgram);

    if (m_gBufferFBO) glDeleteFramebuffers(1, &m_gBufferFBO);
    if (m_gBufferColorTex) glDeleteTextures(1, &m_gBufferColorTex);
    if (m_gBufferDepthRBO) glDeleteRenderbuffers(1, &m_gBufferDepthRBO);

    for (int i = 0; i < 2; ++i) {
        if (m_ssaoFBO[i]) glDeleteFramebuffers(1, &m_ssaoFBO[i]);
        if (m_ssaoTex[i]) glDeleteTextures(1, &m_ssaoTex[i]);
    }
    if (m_aoResultFBO) glDeleteFramebuffers(1, &m_aoResultFBO);
    if (m_aoResultTex) glDeleteTextures(1, &m_aoResultTex);

    if (m_noiseTexture) glDeleteTextures(1, &m_noiseTexture);

    m_gBufferProgram = m_ssaoComputeProgram = m_blurProgram = m_compositeProgram = 0;
    m_gBufferFBO = m_gBufferColorTex = m_gBufferDepthRBO = 0;
    m_ssaoFBO[0] = m_ssaoFBO[1] = 0;
    m_ssaoTex[0] = m_ssaoTex[1] = 0;
    m_aoResultFBO = m_aoResultTex = 0;
    m_noiseTexture = 0;
}

}  // namespace SEASON3B
