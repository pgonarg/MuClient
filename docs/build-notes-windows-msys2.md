# Build Notes — Windows + MSYS2 (Native, No WSL)

This page covers the specific scenario where:
- The repo lives on a **Windows drive** (e.g. `G:\Files\Mu\MuMain`)
- The compiler is **MSYS2 MinGW32** (`C:\msys64\mingw32\bin\i686-w64-mingw32-g++.exe`)
- There is **no WSL** involved
- Builds are triggered from a Windows shell or Claude Code

The existing `build-guide.md` covers WSL + MinGW and CLion + MSVC. This document covers
pitfalls that are unique to the native Windows + MSYS2 combination.

---

## The Single Most Important Rule

> **Always build from the MSYS2 MinGW32 shell, never from PowerShell or cmd.exe.**

When PowerShell or cmd.exe calls `cmake --build`, cmake spawns ninja, ninja spawns `g++.exe`
directly — **without** the MSYS2 runtime environment on `PATH`. The result: `g++.exe` exits
silently with code 1, producing zero error output. This is not a code error; it is the
MSYS2 binary failing to initialize its POSIX layer.

**Wrong (silent failure):**
```powershell
# In PowerShell — g++ is invoked without MSYS2 environment
cmake --build build-mingw --config Release
```

**Correct:**
```bash
# In C:\msys64\mingw32.exe or any MSYS2 MinGW32 terminal
cd /g/Files/Mu/MuMain
cmake --build build-mingw --config Release
```

If you need to trigger builds from PowerShell (e.g. from scripts or Claude Code), wrap the
build in an MSYS2 bash call:
```powershell
& "C:\msys64\usr\bin\bash.exe" -l -c "cd /g/Files/Mu/MuMain && cmake --build build-mingw --config Release 2>&1"
```

---

## Diagnosing "silent" Compile Failures

Ninja shows `FAILED: [code=1]` for a compile step but prints **no error text**. This is the
MSYS2 environment problem described above. To see actual error messages:

1. Open the MSYS2 MinGW32 shell (`C:\msys64\mingw32.exe`).
2. Copy the failed compile command from ninja's output.
3. Paste it into the MSYS2 terminal and run it directly.

Alternatively, run the full build from the MSYS2 shell and errors will appear normally.

---

## dotnet NativeAOT — vswhere.exe Must Be in PATH

The .NET ClientLibrary uses Native AOT, which calls `vswhere.exe` to locate the MSVC linker.
`vswhere.exe` lives at:
```
C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
```

This directory is **not** in the default MSYS2 or system PATH. The CMakeLists.txt works
around this by prepending it when invoking `dotnet publish`:

```cmake
set(VSWHERE_INSTALLER_DIR "C:/Program Files (x86)/Microsoft Visual Studio/Installer")
set(DOTNET_BUILD_PATH "${VSWHERE_INSTALLER_DIR};$ENV{PATH}")
# ...
COMMAND ${CMAKE_COMMAND} -E env "PATH=${DOTNET_BUILD_PATH}" ...
```

If the build fails with `'vswhere.exe' is not recognized`, the path above is wrong for this
machine. Find the correct location with:
```powershell
(Get-Command vswhere.exe -ErrorAction SilentlyContinue).Source
# or scan:
Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio" -Filter vswhere.exe -Recurse
```

Then update `VSWHERE_INSTALLER_DIR` in `CMakeLists.txt`.

---

## dotnet env Overrides — What Breaks It

The CMakeLists.txt uses `cmake -E env` to pass environment variables to `dotnet publish`.
Two variables that **must NOT be overridden**:

| Variable | Effect of overriding |
|----------|----------------------|
| `NUGET_PACKAGES` | Points NuGet at the wrong or empty directory → `Value cannot be null (Parameter 'path1')` from `NuGet.targets(782,5)` |
| `DOTNET_CLI_HOME` | Redirects the .NET CLI home directory → same NuGet null-path crash |

These were removed from both the ClientLibrary and ResxGen `add_custom_command` blocks.
Only `TEMP`, `TMP`, and `PATH` (for vswhere) should be overridden.

---

## GLEW Must Be Linked

`Bloom.cpp` (and any future code using modern OpenGL) calls GLEW functions. GLEW headers
(`<gl/glew.h>`) are included via `stdafx.h`, but the **import library** must be linked.

If the linker reports errors like:
```
undefined reference to `_imp____glewBindFramebuffer'
undefined reference to `_imp____GLEW_VERSION_2_0'
```

Add `glew32` (or `libglew32.a`) to `target_link_libraries` in `CMakeLists.txt`. The import
library should live at `src/dependencies/lib/`.

Check with:
```bash
ls src/dependencies/lib/ | grep -i glew
```

Then in `CMakeLists.txt`, in the MinGW link libraries block:
```cmake
target_link_libraries(Main PRIVATE
    ...
    opengl32
    glu32
    glew32   # <-- add this
    ...
)
```

---

## Current Build State (as of 2026-05-28)

| Step | Status | Notes |
|------|--------|-------|
| CMake configure | ✅ Works | Run from MSYS2 or PowerShell |
| ResxGen (.resx → C++) | ✅ Works | |
| C++ compilation | ✅ Works | **Must run from MSYS2 bash** |
| dotnet ClientLibrary (NativeAOT) | ⚠️ Partially fixed | vswhere PATH fix in place; needs verification |
| Linking | ❌ Fails | `glew32` not yet in `target_link_libraries` |
| Bloom post-process effect | 🔧 In progress | Code written; needs link fix to compile |

---

## Correct Build Command (copy-paste)

Open `C:\msys64\mingw32.exe` and run:

```bash
cd /g/Files/Mu/MuMain
cmake --build build-mingw --config Release --target Main 2>&1 | tail -40
```

Or for a full reconfigure + build:
```bash
cd /g/Files/Mu/MuMain
cmake -S src -B build-mingw -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=src/cmake/toolchains/mingw-w64-i686.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_EDITOR=ON
cmake --build build-mingw --target Main 2>&1 | tail -40
```
