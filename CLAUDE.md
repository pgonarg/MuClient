# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See [`AGENTS.md`](AGENTS.md) — the canonical entry point for AI assistants and contributors. It points at [`docs/CODING_RULES.md`](docs/CODING_RULES.md), which applies to all code changes.

## Critical Build and Development Notes

### Building the Client (ALWAYS follow this)

**DO NOT** try to run cmake directly from bash or use MSYS2 shell indirectly. Instead:

1. Open PowerShell and set the environment:
```powershell
cd 'G:\Files\Mu\MuMain'
$env:MSYSTEM="MINGW32"
$env:PATH="C:\msys64\mingw32\bin;C:\msys64\usr\bin;$env:PATH"
```

2. Configure (first time only):
```powershell
cmake -S . -B build-mingw `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="toolchain-x86.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_EDITOR=ON
```

3. Build:
```powershell
cmake --build build-mingw --config Release --target Main -j8
```

**Why this matters:** Missing the PATH setup or toolchain causes ninja permission errors ("failed recompaction: Permission denied"). The environment MUST be set in the same PowerShell session before cmake runs.

### Post-Build Configuration (DO NOT SKIP)

After every build, update `build-mingw/src/config.ini`:

```ini
[CONNECTION SETTINGS]
ServerIP=192.168.1.66
ServerPort=44406
```

The config defaults to an invalid IP (127.127.127.127). Without this change, the client cannot connect.

### Runtime DLLs

After building, copy MinGW runtime DLLs to `build-mingw/src/`:
```powershell
Copy-Item "C:\msys64\mingw32\bin\libgcc_s_dw2-1.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libstdc++-6.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libwinpthread-1.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libturbojpeg.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
```

## Graphics and Shadows

### Shadow Quality Improvements (5×5 PCF)

Current baseline (commit db8f4e7f): Character and terrain shaders use 5×5 Percentage Closer Filtering (25 samples) instead of 3×3 (9 samples) for softer shadow edges.

**Key implementation details:**
- Bias: 0.0025 (conservative, matches original)
- Equal weighting: All 25 samples count equally
- Divide by 25.0 instead of 9.0 for the final average

**Why this approach works:** Minimal changes to working code. Complex weighted sampling (center=1.0, adjacent=0.8, corner=0.6) caused shadow loss — avoid it.

### Shader Files
- **Character/Terrain Lighting:** `src/source/Render/Shaders/PhongLighting.glsl.h`
  - Contains both character and terrain `computeShadow()` functions
  - Both must be kept in sync for consistent shadow behavior
  - Bias value is critical: too high causes shadow loss, too low causes peter-panning

### Testing Graphics Changes
After building:
1. Run `build-mingw/src/Main.exe`
2. Verify shadows are visible (especially on character models)
3. Check for soft shadow edges (5×5 should be noticeably smoother than 3×3)
4. Look for any flickering or visual artifacts on shadow boundaries

## Common Mistakes to Avoid

1. **Forgetting to set PATH in PowerShell** → ninja permission errors
2. **Not updating config.ini after building** → client can't connect to server
3. **Changing shader bias without testing baseline first** → shadows disappear
4. **Complex shader weight calculations** → tends to break shadow rendering; keep it simple
5. **Applying improvements to only one shader (character OR terrain)** → visual inconsistencies

## References
- Full build documentation: [`BUILD_AND_TEST.md`](BUILD_AND_TEST.md)
- Coding rules: [`docs/CODING_RULES.md`](docs/CODING_RULES.md)
- Build guide: [`docs/build-guide.md`](docs/build-guide.md)
