#pragma once

#include <memory>
#include "GltfLoader.h"

class BMD;

// Unified model interface that can represent either BMD or glTF models
class Model {
public:
    enum class Type {
        BMD,
        GLTF
    };

    virtual ~Model() = default;

    virtual Type GetType() const = 0;

    // For glTF models, return the glTF model data
    virtual gltf::GltfModel* GetGltfModel() { return nullptr; }

    // For BMD models, return the BMD data
    virtual BMD* GetBmdModel() { return nullptr; }
};

// Wrapper for glTF models
class GltfModelWrapper : public Model {
public:
    explicit GltfModelWrapper(std::unique_ptr<gltf::GltfModel> model)
        : m_model(std::move(model)) {}

    Type GetType() const override { return Type::GLTF; }
    gltf::GltfModel* GetGltfModel() override { return m_model.get(); }

private:
    std::unique_ptr<gltf::GltfModel> m_model;
};

// Wrapper for BMD models (compatibility)
class BmdModelWrapper : public Model {
public:
    explicit BmdModelWrapper(BMD* model)
        : m_model(model) {}

    Type GetType() const override { return Type::BMD; }
    BMD* GetBmdModel() override { return m_model; }

private:
    BMD* m_model;
};

// Unified loader that auto-detects format and loads accordingly
class ModelLoader {
public:
    // Load a model from file (auto-detects glTF vs BMD by extension)
    // Returns a Model* that can be queried for type and underlying data
    static std::unique_ptr<Model> Load(const wchar_t* filePath);

    // Load specifically as glTF
    static std::unique_ptr<Model> LoadAsGltf(const wchar_t* filePath);

    // Load specifically as BMD
    static std::unique_ptr<Model> LoadAsBmd(const wchar_t* filePath);
};
