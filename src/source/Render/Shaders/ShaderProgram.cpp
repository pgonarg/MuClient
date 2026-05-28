#include "stdafx.h"
#include "ShaderProgram.h"
#include <fstream>
#include <sstream>

namespace SEASON3B {

ShaderProgram::ShaderProgram()
    : m_ProgramID(0)
{
}

ShaderProgram::~ShaderProgram()
{
    Release();
}

bool ShaderProgram::CompileFromSource(
    const std::string& vertexSrc,
    const std::string& fragmentSrc,
    std::string* outError
)
{
    // Compile vertex shader
    GLuint vertexShader = CompileShader(vertexSrc, GL_VERTEX_SHADER, outError);
    if (!vertexShader) {
        return false;
    }

    // Compile fragment shader
    GLuint fragmentShader = CompileShader(fragmentSrc, GL_FRAGMENT_SHADER, outError);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return false;
    }

    // Link program
    if (!LinkProgram(vertexShader, fragmentShader, outError)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Delete shaders (they're linked into the program)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return true;
}

bool ShaderProgram::CompileFromFiles(
    const std::string& vertexPath,
    const std::string& fragmentPath,
    std::string* outError
)
{
    // Read vertex shader file
    std::ifstream vFile(vertexPath);
    if (!vFile.is_open()) {
        if (outError) {
            *outError = "Failed to open vertex shader file: " + vertexPath;
        }
        return false;
    }
    std::stringstream vBuffer;
    vBuffer << vFile.rdbuf();
    std::string vertexSrc = vBuffer.str();
    vFile.close();

    // Read fragment shader file
    std::ifstream fFile(fragmentPath);
    if (!fFile.is_open()) {
        if (outError) {
            *outError = "Failed to open fragment shader file: " + fragmentPath;
        }
        return false;
    }
    std::stringstream fBuffer;
    fBuffer << fFile.rdbuf();
    std::string fragmentSrc = fBuffer.str();
    fFile.close();

    return CompileFromSource(vertexSrc, fragmentSrc, outError);
}

void ShaderProgram::Use() const
{
    if (m_ProgramID) {
        glUseProgram(m_ProgramID);
    }
}

void ShaderProgram::UseNone() const
{
    glUseProgram(0);
}

GLint ShaderProgram::GetUniformLocation(const std::string& name)
{
    // Check cache first
    auto it = m_UniformCache.find(name);
    if (it != m_UniformCache.end()) {
        return it->second;
    }

    // Query OpenGL
    GLint loc = glGetUniformLocation(m_ProgramID, name.c_str());

    // Cache result (even if -1, so we don't query repeatedly)
    m_UniformCache[name] = loc;

    return loc;
}

void ShaderProgram::SetMatrix3fv(const std::string& name, const float* pMat)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0 && pMat) {
        glUniformMatrix3fv(loc, 1, GL_FALSE, pMat);
    }
}

void ShaderProgram::SetMatrix4fv(const std::string& name, const float* pMat)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0 && pMat) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, pMat);
    }
}

void ShaderProgram::SetVec2f(const std::string& name, float x, float y)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform2f(loc, x, y);
    }
}

void ShaderProgram::SetVec3Array(const std::string& name, int count, const float* values)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0 && count > 0 && values) {
        glUniform3fv(loc, count, values);
    }
}

void ShaderProgram::SetFloatArray(const std::string& name, int count, const float* values)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0 && count > 0 && values) {
        glUniform1fv(loc, count, values);
    }
}

void ShaderProgram::SetVec3f(const std::string& name, float x, float y, float z)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform3f(loc, x, y, z);
    }
}

void ShaderProgram::SetVec3f(const std::string& name, const vec3_t& vec)
{
    SetVec3f(name, vec[0], vec[1], vec[2]);
}

void ShaderProgram::SetVec4f(const std::string& name, float x, float y, float z, float w)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform4f(loc, x, y, z, w);
    }
}

void ShaderProgram::SetFloat(const std::string& name, float value)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform1f(loc, value);
    }
}

void ShaderProgram::SetInt(const std::string& name, int value)
{
    GLint loc = GetUniformLocation(name);
    if (loc >= 0) {
        glUniform1i(loc, value);
    }
}

void ShaderProgram::Release()
{
    if (m_ProgramID) {
        glDeleteProgram(m_ProgramID);
        m_ProgramID = 0;
    }
    m_UniformCache.clear();
}

GLuint ShaderProgram::CompileShader(const std::string& source, GLenum shaderType, std::string* outError)
{
    GLuint shader = glCreateShader(shaderType);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // Check compilation status
    GLint success = 0;
    GLchar infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        if (outError) {
            *outError = std::string("Shader compilation failed (") +
                       GetShaderTypeName(shaderType) + "): " + std::string(infoLog);
        }
        g_ErrorReport.Write(L"> Shader Compilation Error (%S): %S\r\n",
                          GetShaderTypeName(shaderType), infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool ShaderProgram::LinkProgram(GLuint vertexShader, GLuint fragmentShader, std::string* outError)
{
    m_ProgramID = glCreateProgram();
    glAttachShader(m_ProgramID, vertexShader);
    glAttachShader(m_ProgramID, fragmentShader);
    glLinkProgram(m_ProgramID);

    // Check link status
    GLint success = 0;
    GLchar infoLog[512];
    glGetProgramiv(m_ProgramID, GL_LINK_STATUS, &success);

    if (!success) {
        glGetProgramInfoLog(m_ProgramID, 512, nullptr, infoLog);
        if (outError) {
            *outError = std::string("Shader linking failed: ") + std::string(infoLog);
        }
        g_ErrorReport.Write(L"> Shader Link Error: %S\r\n", infoLog);
        glDeleteProgram(m_ProgramID);
        m_ProgramID = 0;
        return false;
    }

    g_ErrorReport.Write(L"> Shader program linked successfully.\r\n");
    return true;
}

const char* ShaderProgram::GetShaderTypeName(GLenum shaderType) const
{
    switch (shaderType) {
    case GL_VERTEX_SHADER:
        return "Vertex";
    case GL_FRAGMENT_SHADER:
        return "Fragment";
    case GL_GEOMETRY_SHADER:
        return "Geometry";
    default:
        return "Unknown";
    }
}

}  // namespace SEASON3B
