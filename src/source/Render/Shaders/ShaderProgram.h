#pragma once

#include <GL/glew.h>
#include <string>
#include <map>

// Math types from MuMain engine
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef float mat3_t[9];   // 3x3 matrix
typedef float mat4_t[16];  // 4x4 matrix

namespace SEASON3B {

class ShaderProgram
{
public:
    ShaderProgram();
    ~ShaderProgram();

    // Compile a shader from source strings
    bool CompileFromSource(
        const std::string& vertexSrc,
        const std::string& fragmentSrc,
        std::string* outError = nullptr
    );

    // Compile a shader from file paths
    bool CompileFromFiles(
        const std::string& vertexPath,
        const std::string& fragmentPath,
        std::string* outError = nullptr
    );

    // Use this shader program
    void Use() const;

    // Disable shader program (use fixed-function)
    void UseNone() const;

    // Check if program is valid
    bool IsValid() const { return m_ProgramID != 0; }

    // Get uniform location (cached for performance)
    GLint GetUniformLocation(const std::string& name);

    // Set uniform matrices (3x3)
    void SetMatrix3fv(const std::string& name, const float* pMat);

    // Set uniform matrices (4x4)
    void SetMatrix4fv(const std::string& name, const float* pMat);

    // Set uniform vectors
    void SetVec2f(const std::string& name, float x, float y);
    void SetVec3f(const std::string& name, float x, float y, float z);
    void SetVec3f(const std::string& name, const vec3_t& vec);

    void SetVec4f(const std::string& name, float x, float y, float z, float w);

    // Set uniform arrays
    void SetVec3Array(const std::string& name, int count, const float* values);   // glUniform3fv
    void SetFloatArray(const std::string& name, int count, const float* values);  // glUniform1fv

    // Set uniform floats
    void SetFloat(const std::string& name, float value);

    // Set uniform integers
    void SetInt(const std::string& name, int value);

    // Get program ID for advanced usage
    GLuint GetProgramID() const { return m_ProgramID; }

    // Cleanup
    void Release();

private:
    GLuint m_ProgramID;
    std::map<std::string, GLint> m_UniformCache;

    // Compile individual shader
    GLuint CompileShader(const std::string& source, GLenum shaderType, std::string* outError);

    // Link shader program
    bool LinkProgram(GLuint vertexShader, GLuint fragmentShader, std::string* outError);

    // Utility to get shader type name for error messages
    const char* GetShaderTypeName(GLenum shaderType) const;
};

}  // namespace SEASON3B
