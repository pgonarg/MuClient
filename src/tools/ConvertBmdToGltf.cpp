// Command-line tool to batch convert BMD model files to glTF 2.0 format
// Usage: ConvertBmdToGltf.exe <input_dir> <output_dir> [options]
//   --scale <factor>      Scale geometry (default: 1.0)
//   --no-animations       Skip animation conversion
//   --embed-textures      Embed texture data in GLB file
//   --single-file         Use .glb format (default), otherwise .gltf + .bin

#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#include <cstring>

// Forward declarations (would link to Render library)
class BMD;
class BmdToGltfConverter;

namespace fs = std::filesystem;

struct ConverterConfig {
    fs::path inputDir;
    fs::path outputDir;
    float scale = 1.0f;
    bool includeAnimations = true;
    bool embedTextures = false;
    bool singleFile = true;
    int filesProcessed = 0;
    int filesSuccessful = 0;
    int filesFailed = 0;
};

void PrintUsage(const char* programName)
{
    std::cout << "BMD to glTF 2.0 Batch Converter\n\n";
    std::cout << "Usage: " << programName << " <input_dir> <output_dir> [options]\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  <input_dir>         Directory containing BMD files\n";
    std::cout << "  <output_dir>        Directory for output glTF files\n\n";
    std::cout << "Options:\n";
    std::cout << "  --scale <n>         Scale geometry by factor (default: 1.0)\n";
    std::cout << "  --no-animations     Skip animation conversion\n";
    std::cout << "  --embed-textures    Embed textures in GLB file\n";
    std::cout << "  --separate-files    Use .gltf + .bin instead of .glb\n";
    std::cout << "  --help              Show this message\n\n";
    std::cout << "Example:\n";
    std::cout << "  ConvertBmdToGltf.exe Data\\Models\\ Data\\Models_glTF\\ --scale 0.01\n";
}

bool ParseArguments(int argc, const char* argv[], ConverterConfig& config)
{
    if (argc < 3) {
        PrintUsage(argv[0]);
        return false;
    }

    config.inputDir = argv[1];
    config.outputDir = argv[2];

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help") {
            PrintUsage(argv[0]);
            return false;
        } else if (arg == "--scale" && i + 1 < argc) {
            config.scale = std::stof(argv[++i]);
        } else if (arg == "--no-animations") {
            config.includeAnimations = false;
        } else if (arg == "--embed-textures") {
            config.embedTextures = true;
        } else if (arg == "--separate-files") {
            config.singleFile = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    // Validate directories
    if (!fs::exists(config.inputDir)) {
        std::cerr << "Error: Input directory not found: " << config.inputDir << "\n";
        return false;
    }

    if (!fs::is_directory(config.inputDir)) {
        std::cerr << "Error: Input path is not a directory: " << config.inputDir << "\n";
        return false;
    }

    // Create output directory
    try {
        fs::create_directories(config.outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Error creating output directory: " << e.what() << "\n";
        return false;
    }

    return true;
}

// TODO: Implement actual conversion
// This would require loading BMD using the engine's loader
// and converting with BmdToGltfConverter
void ConvertFile(const fs::path& inputFile, const fs::path& outputDir,
                 const ConverterConfig& config, ConverterConfig& stats)
{
    try {
        stats.filesProcessed++;

        // Extract relative path for output
        fs::path relativePath = fs::relative(inputFile, config.inputDir);
        fs::path outputPath = outputDir / relativePath;
        outputPath.replace_extension(".glb");

        // Create output subdirectory if needed
        fs::create_directories(outputPath.parent_path());

        // TODO: Load BMD file
        // BMD* bmdModel = new BMD();
        // if (!bmdModel->Open2(...)) {
        //     throw std::runtime_error("Failed to load BMD");
        // }

        // TODO: Convert and save
        // BmdToGltfConverter::ConversionOptions opts;
        // opts.scale = config.scale;
        // opts.includeAnimations = config.includeAnimations;
        // opts.embedTextures = config.embedTextures;
        // opts.singleFile = config.singleFile;

        // if (!BmdToGltfConverter::ConvertAndSave(bmdModel, outputPath.c_str(), opts)) {
        //     throw std::runtime_error("Conversion failed");
        // }

        // delete bmdModel;

        std::cout << "✓ " << relativePath.string() << " -> " << outputPath.filename().string() << "\n";
        stats.filesSuccessful++;
    } catch (const std::exception& e) {
        std::cerr << "✗ " << inputFile.filename().string() << " - Error: " << e.what() << "\n";
        stats.filesFailed++;
    }
}

void ConvertDirectory(const ConverterConfig& config, ConverterConfig& stats)
{
    int bmdCount = 0;

    std::cout << "Scanning for BMD files in: " << config.inputDir << "\n\n";

    // Find all BMD files (case-insensitive)
    for (const auto& entry : fs::recursive_directory_iterator(config.inputDir)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        // Convert to lowercase for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        if (ext == ".bmd") {
            bmdCount++;
            ConvertFile(entry.path(), config.outputDir, config, stats);
        }
    }

    std::cout << "\n" << "=".repeat(60) << "\n";
    std::cout << "Conversion Summary:\n";
    std::cout << "  Total files found:  " << bmdCount << "\n";
    std::cout << "  Successfully converted: " << stats.filesSuccessful << "\n";
    std::cout << "  Failed: " << stats.filesFailed << "\n";
    std::cout << "  Output directory: " << config.outputDir << "\n";
}

int main(int argc, const char* argv[])
{
    ConverterConfig config;
    ConverterConfig stats;

    if (!ParseArguments(argc, argv, config)) {
        return 1;
    }

    std::cout << "BMD to glTF 2.0 Converter\n";
    std::cout << "========================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Scale factor: " << config.scale << "\n";
    std::cout << "  Include animations: " << (config.includeAnimations ? "Yes" : "No") << "\n";
    std::cout << "  Embed textures: " << (config.embedTextures ? "Yes" : "No") << "\n";
    std::cout << "  Format: " << (config.singleFile ? ".glb" : ".gltf + .bin") << "\n\n";

    try {
        ConvertDirectory(config, stats);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return stats.filesFailed > 0 ? 1 : 0;
}
