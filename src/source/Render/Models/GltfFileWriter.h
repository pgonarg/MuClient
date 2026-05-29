#pragma once

#include "GltfLoader.h"
#include <vector>
#include <string>
#include <cstdint>

// Writes glTF models to .glb (binary) or .gltf + .bin format

class GltfFileWriter {
public:
    // Write glTF model to GLB (single-file binary format)
    static bool WriteGlb(const wchar_t* filePath, const gltf::GltfModel& model);

    // Write glTF model to separate .gltf (JSON) + .bin (binary) files
    static bool WriteGltf(const wchar_t* filePath, const gltf::GltfModel& model);

private:
    // Helper: Pack vertex data into binary buffer
    static void PackVertexData(const gltf::GltfModel& model,
                              std::vector<uint8_t>& buffer);

    // Helper: Create glTF JSON structure
    static nlohmann::json CreateGltfJson(const gltf::GltfModel& model,
                                         size_t bufferByteLength);

    // Helper: Write GLB container format
    static bool WriteGlbContainer(const wchar_t* filePath,
                                  const std::string& jsonString,
                                  const std::vector<uint8_t>& binaryData);
};
