#pragma once

/// Tone mapping post-process: converts HDR range to display range (0..1).
///
/// Applies Reinhard tone mapping with optional gamma correction for natural color grading.
/// Integrates after Bloom and SSAO in the post-processing chain.
///
/// Typical per-frame usage (inside RenderMainScene):
///
///   Bloom::BeginCapture();              // bind scene FBO
///   SetupMainSceneViewport(...);
///   RenderGameWorld(...);
///   Bloom::ApplyBloom();                // bloom pass
///   SSAOManager::ApplySSAO(...);        // SSAO pass
///   ToneMapping::Apply(sceneColorTex);  // tone map final result
///   RenderMainSceneUI();                // 2-D UI on top
///
class ToneMapping
{
public:
    /// One-time setup: compile shaders. Must be called after glewInit().
    static bool Initialize();

    /// Release all GPU resources.
    static void Shutdown();

    /// Apply tone mapping to the scene texture. Renders result to default framebuffer.
    /// Input: sceneColorTex = the final scene color (after Bloom, SSAO, etc.)
    /// Returns true on success.
    static bool Apply(unsigned int sceneColorTex);

    // ── Tunable parameters ──────────────────────────────────────────────────────

    static bool  IsEnabled()           { return s_enabled; }
    static void  SetEnabled(bool v)    { s_enabled = v; }

    /// Tone mapping strength [0..1]. 0 = no tone mapping, 1 = full effect.
    static float GetStrength()         { return s_strength; }
    static void  SetStrength(float s)  { s_strength = s; }

    /// Gamma correction (typically 2.2 for sRGB). Lower = darker, higher = brighter.
    static float GetGamma()            { return s_gamma; }
    static void  SetGamma(float g)     { s_gamma = g; }

    /// Exposure adjustment [0.1..3.0]. Lower = darker/moodier, higher = brighter.
    static float GetExposure()         { return s_exposure; }
    static void  SetExposure(float e)  { s_exposure = e; }

    /// Color tint [R, G, B]. Shift colors for mood: warmer (1.2, 1.0, 0.8) or cooler (0.8, 0.9, 1.2).
    static void  SetColorTint(float r, float g, float b) { s_tintR = r; s_tintG = g; s_tintB = b; }
    static void  GetColorTint(float& r, float& g, float& b) { r = s_tintR; g = s_tintG; b = s_tintB; }

    /// Contrast boost [0.5..2.0]. Lower = flatter, higher = more dramatic.
    static float GetContrast()         { return s_contrast; }
    static void  SetContrast(float c)  { s_contrast = c; }

    // ── Tone Mapping Presets (20 different moods) ────────────────────────────────
    static void PresetOff();              // 0: No tone mapping
    static void PresetSubtleDark();       // 1: Subtle darkening
    static void PresetDarkCinema();       // 2: Dark cinematic
    static void PresetMoody();            // 3: Moody atmosphere
    static void PresetNoir();             // 4: Noir grayscale
    static void PresetWarmSepia();        // 5: Warm sepia tones
    static void PresetCoolMystery();      // 6: Cool mysterious
    static void PresetDesaturated();      // 7: Desaturated muted
    static void PresetHighContrast();     // 8: High contrast drama
    static void PresetBrightVivid();      // 9: Bright and vivid
    static void PresetHorror();           // 10: Dark horror
    static void PresetSunset();           // 11: Warm sunset
    static void PresetDeepSea();          // 12: Deep water blue
    static void PresetDusty();            // 13: Dusty earth tones
    static void PresetNeon();             // 14: Neon bright
    static void PresetRetro();            // 15: Retro game look
    static void PresetBleachBypass();     // 16: Bleach bypass film
    static void PresetCyberpunk();        // 17: Cyberpunk neon
    static void PresetGoldenHour();       // 18: Warm golden hour
    static void PresetTwilight();         // 19: Purple twilight

    static int GetCurrentPreset()         { return s_currentPreset; }
    static void CyclePreset(int direction); // direction: 1=next, -1=prev

private:
    static bool  CompileShaders();
    static void  DeleteShaders();
    static unsigned int CompileShader(unsigned int type, const char* src);
    static unsigned int LinkProgram(unsigned int vert, unsigned int frag);
    static void  RenderFullscreenQuad();

    // ── State ───────────────────────────────────────────────────────────────────
    static bool  s_initialized;
    static bool  s_enabled;
    static float s_strength;
    static float s_gamma;
    static float s_exposure;
    static float s_tintR, s_tintG, s_tintB;
    static float s_contrast;
    static int   s_currentPreset;

    // Shader program
    static unsigned int s_toneMappingProgram;

    // FBO for capturing scene when Bloom is off
    static unsigned int s_sceneFbo;
    static unsigned int s_sceneColorTex;
    static int s_fboWidth, s_fboHeight;

    // Helper to create/recreate FBO
    static bool CreateSceneFBO(int width, int height);
    static void DestroySceneFBO();
};
