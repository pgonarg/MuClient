#include "GltfFileWriter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <algorithm>

using json = nlohmann::json;

bool GltfFileWriter::WriteGlb(const wchar_t* filePath, const gltf::GltfModel& model)
{
    if (!filePath) return false;
    if (model.meshes.empty()) return false;

    // Step 1: Pack all binary data
    std::vector<uint8_t> binaryData;
    PackVertexData(model, binaryData);

    if (binaryData.empty()) return false;

    // Step 2: Create JSON structure
    json gltfJson = CreateGltfJson(model, binaryData.size());
    std::string jsonString = gltfJson.dump();

    // Step 3: Write GLB container
    return WriteGlbContainer(filePath, jsonString, binaryData);
}

bool GltfFileWriter::WriteGltf(const wchar_t* filePath, const gltf::GltfModel& model)
{
    // TODO: Implement separate .gltf + .bin format
    // For now, redirect to GLB
    return WriteGlb(filePath, model);
}

void GltfFileWriter::PackVertexData(const gltf::GltfModel& model,
                                   std::vector<uint8_t>& buffer)
{
    // Calculate total counts
    size_t totalVertices = 0;
    size_t totalIndices = 0;

    for (const auto& mesh : model.meshes) {
        totalVertices += mesh.vertices.size();
        totalIndices += mesh.indices.size();
    }

    if (totalVertices == 0) return;

    // Reserve space (rough estimate)
    buffer.reserve(totalVertices * 32 + totalIndices * 4);

    // Pack positions (VEC3 = 3 floats = 12 bytes per vertex)
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (int i = 0; i < 3; i++) {
                float value = vertex.position[i];
                buffer.insert(buffer.end(),
                             (uint8_t*)&value,
                             (uint8_t*)&value + 4);
            }
        }
    }

    // Pack normals (VEC3 = 3 floats = 12 bytes per vertex)
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (int i = 0; i < 3; i++) {
                float value = vertex.normal[i];
                buffer.insert(buffer.end(),
                             (uint8_t*)&value,
                             (uint8_t*)&value + 4);
            }
        }
    }

    // Pack UV coordinates (VEC2 = 2 floats = 8 bytes per vertex)
    for (const auto& mesh : model.meshes) {
        for (const auto& vertex : mesh.vertices) {
            for (int i = 0; i < 2; i++) {
                float value = vertex.texCoord[i];
                buffer.insert(buffer.end(),
                             (uint8_t*)&value,
                             (uint8_t*)&value + 4);
            }
        }
    }

    // Pack indices (uint32 = 4 bytes per index)
    for (const auto& mesh : model.meshes) {
        for (uint32_t idx : mesh.indices) {
            buffer.insert(buffer.end(),
                         (uint8_t*)&idx,
                         (uint8_t*)&idx + 4);
        }
    }
}

json GltfFileWriter::CreateGltfJson(const gltf::GltfModel& model,
                                   size_t bufferByteLength)
{
    json gltf;

    // Asset metadata
    gltf["asset"]["version"] = "2.0";
    gltf["asset"]["generator"] = "MuUnique BMD→glTF Converter v1.0";

    // Scene setup
    gltf["scene"] = 0;
    gltf["scenes"] = json::array();
    gltf["scenes"][0]["nodes"] = json::array();

    for (size_t i = 0; i < model.meshes.size(); i++) {
        gltf["scenes"][0]["nodes"].push_back((int)i);
    }

    // Nodes (one per mesh for simplicity)
    gltf["nodes"] = json::array();
    for (size_t i = 0; i < model.meshes.size(); i++) {
        json node;
        node["mesh"] = (int)i;
        node["name"] = model.meshes[i].name;
        gltf["nodes"].push_back(node);
    }

    // Calculate accessor byte offsets
    size_t totalVertices = 0;
    for (const auto& mesh : model.meshes) {
        totalVertices += mesh.vertices.size();
    }

    size_t offsetPositions = 0;
    size_t offsetNormals = totalVertices * 12;
    size_t offsetTexCoords = offsetNormals + totalVertices * 12;
    size_t offsetIndices = offsetTexCoords + totalVertices * 8;

    // Accessors
    gltf["accessors"] = json::array();

    // Position accessor
    json posAccessor;
    posAccessor["bufferView"] = 0;
    posAccessor["byteOffset"] = 0;
    posAccessor["componentType"] = 5126;  // FLOAT
    posAccessor["count"] = totalVertices;
    posAccessor["type"] = "VEC3";
    gltf["accessors"].push_back(posAccessor);

    // Normal accessor
    json normAccessor;
    normAccessor["bufferView"] = 1;
    normAccessor["byteOffset"] = 0;
    normAccessor["componentType"] = 5126;  // FLOAT
    normAccessor["count"] = totalVertices;
    normAccessor["type"] = "VEC3";
    gltf["accessors"].push_back(normAccessor);

    // TexCoord accessor
    json texAccessor;
    texAccessor["bufferView"] = 2;
    texAccessor["byteOffset"] = 0;
    texAccessor["componentType"] = 5126;  // FLOAT
    texAccessor["count"] = totalVertices;
    texAccessor["type"] = "VEC2";
    gltf["accessors"].push_back(texAccessor);

    // Index accessor
    size_t totalIndices = 0;
    for (const auto& mesh : model.meshes) {
        totalIndices += mesh.indices.size();
    }

    json idxAccessor;
    idxAccessor["bufferView"] = 3;
    idxAccessor["byteOffset"] = 0;
    idxAccessor["componentType"] = 5125;  // UNSIGNED_INT
    idxAccessor["count"] = totalIndices;
    idxAccessor["type"] = "SCALAR";
    gltf["accessors"].push_back(idxAccessor);

    // Buffer Views
    gltf["bufferViews"] = json::array();

    json posBufferView;
    posBufferView["buffer"] = 0;
    posBufferView["byteOffset"] = offsetPositions;
    posBufferView["byteLength"] = totalVertices * 12;
    gltf["bufferViews"].push_back(posBufferView);

    json normBufferView;
    normBufferView["buffer"] = 0;
    normBufferView["byteOffset"] = offsetNormals;
    normBufferView["byteLength"] = totalVertices * 12;
    gltf["bufferViews"].push_back(normBufferView);

    json texBufferView;
    texBufferView["buffer"] = 0;
    texBufferView["byteOffset"] = offsetTexCoords;
    texBufferView["byteLength"] = totalVertices * 8;
    gltf["bufferViews"].push_back(texBufferView);

    json idxBufferView;
    idxBufferView["buffer"] = 0;
    idxBufferView["byteOffset"] = offsetIndices;
    idxBufferView["byteLength"] = totalIndices * 4;
    gltf["bufferViews"].push_back(idxBufferView);

    // Meshes
    gltf["meshes"] = json::array();
    for (size_t i = 0; i < model.meshes.size(); i++) {
        json mesh;
        mesh["name"] = model.meshes[i].name;
        mesh["primitives"] = json::array();

        json primitive;
        primitive["attributes"]["POSITION"] = 0;
        primitive["attributes"]["NORMAL"] = 1;
        primitive["attributes"]["TEXCOORD_0"] = 2;
        primitive["indices"] = 3;
        primitive["mode"] = 4;  // TRIANGLES

        mesh["primitives"].push_back(primitive);
        gltf["meshes"].push_back(mesh);
    }

    // Buffer
    gltf["buffers"] = json::array();
    json buffer;
    buffer["byteLength"] = bufferByteLength;
    gltf["buffers"].push_back(buffer);

    return gltf;
}

bool GltfFileWriter::WriteGlbContainer(const wchar_t* filePath,
                                      const std::string& jsonString,
                                      const std::vector<uint8_t>& binaryData)
{
    if (!filePath) return false;

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    // Calculate chunk sizes
    uint32_t jsonChunkLen = (uint32_t)jsonString.size();
    uint32_t jsonChunkLenPadded = ((jsonChunkLen + 3) / 4) * 4;  // 4-byte align
    uint32_t jsonPaddingSize = jsonChunkLenPadded - jsonChunkLen;

    uint32_t binChunkLen = (uint32_t)binaryData.size();
    uint32_t binChunkLenPadded = ((binChunkLen + 3) / 4) * 4;  // 4-byte align
    uint32_t binPaddingSize = binChunkLenPadded - binChunkLen;

    // Total file size: header(12) + json chunk header(8) + json data + bin chunk header(8) + bin data
    uint32_t totalFileSize = 12 + 8 + jsonChunkLenPadded + 8 + binChunkLenPadded;

    // Write GLB header
    uint32_t magic = 0x46546C67;      // "glTF"
    uint32_t version = 2;

    file.write((const char*)&magic, 4);
    file.write((const char*)&version, 4);
    file.write((const char*)&totalFileSize, 4);

    // Write JSON chunk
    uint32_t jsonType = 0x4E4F534A;   // "JSON"
    file.write((const char*)&jsonChunkLenPadded, 4);
    file.write((const char*)&jsonType, 4);
    file.write(jsonString.c_str(), jsonChunkLen);

    // Pad JSON to 4-byte boundary
    for (uint32_t i = 0; i < jsonPaddingSize; i++) {
        file.write(" ", 1);
    }

    // Write binary chunk
    uint32_t binType = 0x00004E42;    // "BIN\0"
    file.write((const char*)&binChunkLen, 4);
    file.write((const char*)&binType, 4);
    file.write((const char*)binaryData.data(), binChunkLen);

    // Pad binary to 4-byte boundary
    for (uint32_t i = 0; i < binPaddingSize; i++) {
        file.write("\0", 1);
    }

    file.close();
    return true;
}
