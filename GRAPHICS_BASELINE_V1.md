# MuOnline Season 6 - Graphics Baseline v1.0

**Date:** 2026-05-28  
**Status:** ✅ COMPLETE & COMMITTED  
**Baseline:** Established for future graphics iterations

---

## Overview

After extensive testing and iteration, we've established a stable graphics baseline featuring modern shader-based rendering with careful optimization of lighting and ambient effects.

## Current Configuration

### ✅ Enabled Features

#### 1. **Shader-Based Phong Lighting**
- Per-pixel Phong lighting model (Ambient + Diffuse + Specular)
- Dynamic point lights for interactive lighting
- Specular highlights with configurable shininess
- Rim lighting for edge detail

**File:** `src/source/Render/Shaders/PhongLighting.glsl.h`

#### 2. **Ambient Occlusion (SSAO)**
- Screen-Space Ambient Occlusion for depth and crevice darkening
- 8-tap sampling with random kernel rotation
- Bilateral blur for edge-preserving smoothing
- Currently enabled by default with subtle intensity (0.3)

**Files:** 
- `src/source/Render/Shaders/SSAOManager.h/cpp`
- `src/source/Render/Shaders/SSAOShader.glsl.h`

#### 3. **Dimmed Directional Lighting**
- Directional light reduced from 0.8× to 0.4× brightness
- Ambient floor reduced from 0.4 to 0.2
- Minimum luminosity reduced from 0.2 to 0.15
- **Result:** Light emitters (torches, spells, effects) are more prominent

**File:** `src/source/Render/Shaders/PhongLighting.glsl.h` (lines 117-118)

#### 4. **Tone Mapping Infrastructure**
- Reinhard tone mapping with gamma correction
- Independent FBO system (works with or without Bloom)
- 20 different tone mapping presets
- Keyboard control: Arrow keys to cycle, 0-9 for preset shortcuts
- **Currently Disabled:** s_enabled = false (too much color banding when enabled)

**Files:**
- `src/source/Render/PostProcess/ToneMapping.h/cpp`
- Available presets: Off, Subtle Dark, Dark Cinema, Moody, Noir, Warm Sepia, Cool Mystery, Desaturated, High Contrast, Bright Vivid, Horror, Sunset, Deep Sea, Dusty, Neon, Retro, Bleach Bypass, Cyberpunk, Golden Hour, Twilight

#### 5. **Shadow Mapping**
- Directional shadow map (2048×2048)
- Per-character shadow rendering (single clean shadow)
- Dual-rendering eliminated (shader-only path)

**File:** `src/source/Render/Shaders/ShadowMap.h/cpp`

---

## ❌ Disabled Features

### Bloom (Post-Processing Glow)
- **Reason:** Composite shader causes "plainly lit" effect—whole scene becomes overexposed
- **Status:** Disabled by default (s_enabled = false in Bloom.cpp)
- **Note:** Infrastructure intact; can be debugged/fixed in future iteration
- **File:** `src/source/Render/PostProcess/Bloom.cpp`

### Tone Mapping (Color Grading)
- **Reason:** All 20 presets tested; all showed excessive color banding/posterization
- **Status:** Disabled by default (s_enabled = false in ToneMapping.cpp)
- **Note:** Full preset system + keyboard control implemented; infrastructure ready for future refinement
- **Files:** `src/source/Render/PostProcess/ToneMapping.h/cpp`

---

## Build Info

- **Executable:** `build-mingw/src/Main.exe` (33.5 MB)
- **Compiler:** MinGW (GCC)
- **OpenGL:** 2.1 / GLSL 1.20 (compatibility profile)
- **Server:** 192.168.1.66:44406 (OpenMU)

---

## Known Limitations & Future Work

### Visual Quality Opportunities
1. **Normal Mapping** — Add texture detail to character skins
2. **Parallax Mapping** — Armor/equipment depth detail
3. **Screen-Space Reflections** — Wet surface reflections
4. **Improved Shadow Quality** — Better anti-aliasing on shadow edges
5. **Volumetric Lighting** — God rays effect

### Post-Processing to Revisit
1. **Bloom Composite** — Debug the "plainly lit" effect when enabled
2. **Tone Mapping** — Simpler presets; avoid posterization (may need different approach than current Reinhard)
3. **Color Grading** — LUT-based color grading as alternative to tone mapping

### Performance Opportunities
- Profile SSAO overhead (target <2ms)
- Consider SSAO radius/sample tuning for specific hardware
- Investigate Bloom composite issue (possible FBO texture read/write conflict)

---

## Testing Notes

- ✅ SSAO: Visible, works well (subtle depth effect)
- ✅ Phong lighting: Clean, no flickering, proper shading
- ✅ Dimmed directional: Light emitters pop well, good mood
- ✅ Shadows: Single, clean shadow per character
- ⚠️ Bloom: Breaks image (disabled)
- ⚠️ Tone Mapping: All presets posterize colors (disabled)

---

## How to Use This Baseline

### For Future Graphics Work
1. **Extend features** from this baseline (new shaders, effects)
2. **Profile performance** against these metrics
3. **Keep disabled features disabled** until root causes are fixed
4. **Test on the server** (192.168.1.66:44406) before committing

### To Re-Enable Tone Mapping for Testing
```cpp
// In src/source/Render/PostProcess/ToneMapping.cpp, line ~20:
bool ToneMapping::s_enabled = true;  // Change false → true
```
Then rebuild. Use Arrow Keys/0-9 to cycle presets in-game.

### To Debug Bloom
1. Check Bloom::ApplyBloom() composite shader in `src/source/Render/PostProcess/Bloom.cpp`
2. Verify FBO texture binding and framebuffer state
3. Consider whether scene capture should use different format/filtering

---

## Commit Hash

Use this commit as your baseline reference for all future graphics work:

```
git log --oneline | grep "baseline"
```

Graphics improvements locked in. Baseline established. Ready for next iteration.
