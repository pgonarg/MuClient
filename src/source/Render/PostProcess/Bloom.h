#pragma once

/// Post-processing bloom / glow pass.
///
/// Wraps the 3D world render in an off-screen FBO, extracts bright pixels,
/// Gaussian-blurs them, and additively composites the result before the 2D UI
/// draws on top.
///
/// Typical per-frame usage (inside RenderMainScene):
///
///   Bloom::BeginCapture();              // bind scene FBO, clear it
///   SetupMainSceneViewport(...);        // sets up GL matrices inside the FBO
///   RenderGameWorld(...);               // 3-D scene → FBO texture
///   Bloom::ApplyBloom();               // threshold → blur → composite → default FB
///   RenderMainSceneUI();               // 2-D UI on top, unaffected by bloom
///
/// Initialize() must be called once after glewInit(). All other functions are
/// safe no-ops if initialization failed or bloom is disabled.
class Bloom
{
public:
    /// One-time setup: compile shaders, verify extension support.
    /// Returns true on success. Must be called after glewInit().
    static bool Initialize();

    /// Release all GPU resources.
    static void Shutdown();

    /// Bind the scene FBO and clear it so RenderGameWorld draws into it.
    /// Lazily recreates FBOs when the window size changes.
    static void BeginCapture();

    /// Run threshold → blur → composite passes, then unbind the FBO so the
    /// next call (UI render) goes directly to the default framebuffer.
    static void ApplyBloom();

    // ── Tuneable parameters (safe to change at runtime) ──────────────────────

    static bool  IsEnabled()           { return s_enabled; }
    static void  SetEnabled(bool v)    { s_enabled = v; }

    /// Luminance threshold [0..1]. Pixels brighter than this contribute to
    /// bloom. Lower = more bloom, higher = only very bright pixels bloom.
    static float GetThreshold()            { return s_threshold; }
    static void  SetThreshold(float t)     { s_threshold = t; }

    /// Additive strength of the blurred glow layer. 1.0 = full brightness,
    /// values above 1.0 over-brighten intentionally for a stylised look.
    static float GetStrength()             { return s_strength; }
    static void  SetStrength(float s)      { s_strength = s; }

    /// Number of horizontal+vertical blur iterations [1..8].
    /// More passes = softer / wider bloom (also slightly more GPU cost).
    static int   GetBlurPasses()           { return s_blurPasses; }
    static void  SetBlurPasses(int n)
    {
        s_blurPasses = (n < 1) ? 1 : (n > 8) ? 8 : n;
    }

    // ── Accessors for post-processing integration (SSAO, etc.) ──────────────────
    /// Get the scene color texture (rendered 3D world before bloom composite)
    static unsigned int GetSceneColorTexture() { return s_sceneColorTex; }
    /// Get the scene depth renderbuffer (for depth-based post-effects like SSAO)
    static unsigned int GetSceneDepthRBO()     { return s_sceneDepthRbo; }
    /// Get current FBO dimensions
    static void GetFBODimensions(int& outWidth, int& outHeight)
    {
        outWidth = s_fboWidth;
        outHeight = s_fboHeight;
    }

private:
    static bool  CreateFBOs(int width, int height);
    static void  DestroyFBOs();
    static bool  CompileShaders();
    static void  DeleteShaders();
    static unsigned int CompileShader(unsigned int type, const char* src);
    static unsigned int LinkProgram(unsigned int vert, unsigned int frag);
    static void  RenderFullscreenQuad();

    // ── State ─────────────────────────────────────────────────────────────────
    static bool  s_initialized;
    static bool  s_enabled;
    static float s_threshold;
    static float s_strength;
    static int   s_blurPasses;

    // FBO dimensions (track to detect window resize)
    static int   s_fboWidth;
    static int   s_fboHeight;

    // Scene capture (full resolution)
    static unsigned int s_sceneFbo;
    static unsigned int s_sceneColorTex;
    static unsigned int s_sceneDepthRbo;

    // Ping-pong bloom buffers (half resolution)
    static unsigned int s_bloomFbo[2];
    static unsigned int s_bloomTex[2];

    // GLSL programs
    static unsigned int s_thresholdProg;
    static unsigned int s_blurProg;
    static unsigned int s_compositeProg;
};
