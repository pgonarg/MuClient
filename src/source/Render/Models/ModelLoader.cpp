#include "stdafx.h"
#include "ModelLoader.h"
#include "GltfLoader.h"
#include "ZzzBMD.h"
#include <cctype>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// Helper: Convert wide string to lowercase
static std::wstring ToLower(const wchar_t* str) {
    std::wstring result(str);
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

// Helper: Get file extension
static std::wstring GetExtension(const wchar_t* filePath) {
    std::wstring path(filePath);
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) return L"";
    return ToLower(path.substr(dotPos).c_str());
}

// Helper: Get base name without extension
static std::wstring GetBaseName(const wchar_t* filePath) {
    std::wstring path(filePath);
    size_t lastSlash = path.find_last_of(L"\\/");
    std::wstring filename = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
    size_t dotPos = filename.find_last_of(L'.');
    if (dotPos == std::wstring::npos) return filename;
    return filename.substr(0, dotPos);
}

// Helper: Check if file exists
static bool FileExists(const wchar_t* filePath) {
    try {
        return fs::exists(filePath);
    } catch (...) {
        return false;
    }
}

std::unique_ptr<Model> ModelLoader::Load(const wchar_t* filePath) {
    if (!filePath) return nullptr;

    std::wstring ext = GetExtension(filePath);

    // For glTF files, load directly
    if (ext == L".glb" || ext == L".gltf") {
        return LoadAsGltf(filePath);
    }

    // For BMD files, check for glTF version first (converted/edited assets)
    if (ext == L".bmd") {
        // Extract base name (e.g., "character" from "character.bmd")
        std::wstring baseName = GetBaseName(filePath);

        // Check for glTF version in Models_gltf/ folder
        // Try .glb first (single file), then .gltf (with .bin)
        std::wstring gltfPath_glb = L"Data/Models_gltf/" + baseName + L".glb";
        std::wstring gltfPath_gltf = L"Data/Models_gltf/" + baseName + L".gltf";

        if (FileExists(gltfPath_glb.c_str())) {
            // Found converted glTF version - load it instead of BMD
            auto model = LoadAsGltf(gltfPath_glb.c_str());
            if (model) {
                // Successfully loaded glTF (modified asset)
                return model;
            }
        } else if (FileExists(gltfPath_gltf.c_str())) {
            // Found separate .gltf + .bin version
            auto model = LoadAsGltf(gltfPath_gltf.c_str());
            if (model) {
                return model;
            }
        }

        // No glTF version found, load original BMD
        return LoadAsBmd(filePath);
    }

    // Unknown extension: try glTF first, then BMD
    auto gltfModel = gltf::GltfLoader::Load(filePath);
    if (gltfModel) {
        return std::make_unique<GltfModelWrapper>(std::move(gltfModel));
    }

    // Fall back to BMD
    return LoadAsBmd(filePath);
}

std::unique_ptr<Model> ModelLoader::LoadAsGltf(const wchar_t* filePath) {
    auto gltfModel = gltf::GltfLoader::Load(filePath);
    if (!gltfModel) {
        return nullptr;
    }
    return std::make_unique<GltfModelWrapper>(std::move(gltfModel));
}

std::unique_ptr<Model> ModelLoader::LoadAsBmd(const wchar_t* filePath) {
    // Extract directory and filename from path
    std::wstring path(filePath);
    size_t lastSlash = path.find_last_of(L"\\/");
    std::wstring dir = (lastSlash == std::wstring::npos) ? L"" : path.substr(0, lastSlash);
    std::wstring filename = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);

    BMD* bmd = new BMD();

    // Try to open the BMD file
    // The Open2 method expects directory and filename separately
    if (bmd->Open2(dir.c_str(), filename.c_str())) {
        return std::make_unique<BmdModelWrapper>(bmd);
    }

    delete bmd;
    return nullptr;
}
