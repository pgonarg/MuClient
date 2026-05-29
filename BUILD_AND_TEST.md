# Build and Test Guide

This document describes how to build the client executable and connect it to the OpenMU server for testing.

## Prerequisites

- **MSYS2/MinGW**: Installed at `C:\msys64\mingw32`
- **.NET SDK 10.0+**: For building the Client Library DLL
- **CMake**: For configuring the build
- **Git**: For version control

## Building the Client

### Step 1: Configure the Build

From PowerShell in the project root:

```powershell
cd G:\Files\Mu\MuMain
$env:MSYSTEM="MINGW32"
$env:PATH="C:\msys64\mingw32\bin;C:\msys64\usr\bin;$env:PATH"

cmake -S . -B build-mingw `
  -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="toolchain-x86.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_EDITOR=ON
```

**Note:** If you get a turbojpeg error during configuration, it's expected. The build will use the DLL import library instead of static. This is handled by a workaround in `src/CMakeLists.txt` (line 520-522).

### Step 2: Build the Executable

```powershell
$env:MSYSTEM="MINGW32"
$env:PATH="C:\msys64\mingw32\bin;C:\msys64\usr\bin;$env:PATH"
cmake --build build-mingw --config Release --target Main
```

**Output:**
- Executable: `build-mingw\src\Main.exe` (≈20MB)
- Client Library DLL: `build-mingw\src\MUnique.Client.Library.dll`
- Configuration: `build-mingw\src\config.ini`

### Step 3: Copy Required Runtime DLLs

The following MinGW runtime DLLs must be in the same directory as Main.exe:

```powershell
Copy-Item "C:\msys64\mingw32\bin\libgcc_s_dw2-1.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libstdc++-6.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libwinpthread-1.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
Copy-Item "C:\msys64\mingw32\bin\libturbojpeg.dll" "G:\Files\Mu\MuMain\build-mingw\src\" -Force
```

## Running the Server

The OpenMU server runs on a remote machine at `192.168.1.66`.

### Starting the Server (on 192.168.1.66)

```bash
ssh -i ~/.ssh/mu_arcade ARCADE@192.168.1.66 \
  "cd C:/MuServer/OpenMU/src/Startup/bin/Release && \
   MUnique.OpenMU.Startup.exe -autostart -daemon -resolveIP:192.168.1.66"
```

**Server Status:**
- Chat Server: Running
- Game Servers (3): Listening on ports 55901-55906
- Connect Server: Listening on ports 44405-44406
- Admin Panel: http://192.168.1.66:5000

## Running the Client

### Configuration

Edit `build-mingw\src\config.ini`:

```ini
[CONNECTION SETTINGS]
ServerIP=192.168.1.66
ServerPort=44406
```

### Launching

Simply run:
```
G:\Files\Mu\MuMain\build-mingw\src\Main.exe
```

The client will connect to the server at `192.168.1.66:44406`.

## Testing Graphics Changes

### What to Test

When the client connects, test the following for the translucent object flickering issue:

1. **Character Rendering**: Look for flickering on translucent character parts (cloaks, wings, etc.)
2. **Environment Objects**: Check translucent game objects
3. **Shadow Rendering**: Verify that shadows don't cause flickering on transparent surfaces
4. **Performance**: Monitor FPS in the `/details` overlay (`$details on` in chat)

### Known Issues

- **Point Lights Diagnostic**: Point lights are currently disabled (line 230 in `src/source/Render/Shaders/ShaderLibrary.cpp`) for testing. This was left as a temporary diagnostic for wing flickering.
- **Turbojpeg Library**: Using DLL import library instead of static (see line 520-522 in `src/CMakeLists.txt`)

## Troubleshooting

### "libgcc_s_dw2-1.dll was not found"
Copy the MinGW runtime DLLs to `build-mingw\src\` (see Step 3 above)

### "libturbojpeg.dll was not found"
Copy from `C:\msys64\mingw32\bin\libturbojpeg.dll` to `build-mingw\src\`

### Client won't connect to server
1. Verify `config.ini` has `ServerIP=192.168.1.66`
2. Check that the server is running on 192.168.1.66
3. Verify the Connect Server is listening on port 44406

### Build configuration fails with turbojpeg error
This is normal and expected. The workaround allows building with the DLL import library. See line 520-522 in `src/CMakeLists.txt`.

## Build Artifacts

After a successful build, the directory structure is:

```
build-mingw/
├── src/
│   ├── Main.exe                          (Client executable)
│   ├── MUnique.Client.Library.dll        (C# Network library)
│   ├── config.ini                        (Client configuration)
│   ├── *.dll                             (Runtime and asset DLLs)
│   └── bin/                              (Game assets - copied from src/bin)
├── Generated/                            (Code generation output)
└── CMakeFiles/                           (CMake internal files)
```

## References

- [build-guide.md](docs/build-guide.md) - Detailed build system documentation
- [PHASE1_PROGRESS.md](PHASE1_PROGRESS.md) - Shader system implementation progress
- [AGENTS.md](AGENTS.md) - Code style and contribution guidelines
