#include "stdafx.h"
#include "ModelLoader.h"
#include "GltfLoader.h"
#include "ZzzBMD.h"
#include <cctype>
#include <algorithm>

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

std::unique_ptr<Model> ModelLoader::Load(const wchar_t* filePath) {
    std::wstring ext = GetExtension(filePath);

    if (ext == L".glb" || ext == L".gltf") {
        return LoadAsGltf(filePath);
    } else if (ext == L".bmd") {
        return LoadAsBmd(filePath);
    }

    // Default: try glTF first, then BMD
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
