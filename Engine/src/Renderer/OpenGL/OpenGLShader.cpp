#include "Xora2D/Renderer/Shader.h"

#include "Xora2D/Core/Logger.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace Xora2D {
namespace {

std::string ReadTextFile(const std::string& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

GLuint CompileShader(GLenum type, const std::string& source, const std::string& label) {
    const GLuint shader = glCreateShader(type);
    const char* sourceCStr = source.c_str();
    glShaderSource(shader, 1, &sourceCStr, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string message(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, message.data());
    glDeleteShader(shader);
    throw std::runtime_error(label + " shader compile error:\n" + message);
}

GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, const std::string& name) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string message(static_cast<std::size_t>(logLength > 1 ? logLength : 1), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, message.data());
    glDeleteProgram(program);
    throw std::runtime_error(name + " shader link error:\n" + message);
}

class OpenGLShader final : public Shader {
public:
    OpenGLShader(std::string name, GLuint program)
        : m_name(std::move(name)), m_program(program) {}

    ~OpenGLShader() override {
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
    }

    OpenGLShader(const OpenGLShader&) = delete;
    OpenGLShader& operator=(const OpenGLShader&) = delete;

    void Bind() const override { glUseProgram(m_program); }
    void Unbind() const override { glUseProgram(0); }

    void SetInt(const std::string& name, int value) override {
        glUniform1i(GetUniformLocation(name), value);
    }

    void SetFloat(const std::string& name, float value) override {
        glUniform1f(GetUniformLocation(name), value);
    }

    void SetVec2(const std::string& name, const glm::vec2& value) override {
        glUniform2fv(GetUniformLocation(name), 1, glm::value_ptr(value));
    }

    void SetVec3(const std::string& name, const glm::vec3& value) override {
        glUniform3fv(GetUniformLocation(name), 1, glm::value_ptr(value));
    }

    void SetVec4(const std::string& name, const glm::vec4& value) override {
        glUniform4fv(GetUniformLocation(name), 1, glm::value_ptr(value));
    }

    void SetMat3(const std::string& name, const glm::mat3& value) override {
        glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
    }

    void SetMat4(const std::string& name, const glm::mat4& value) override {
        glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
    }

    const std::string& GetName() const override { return m_name; }
    std::uint32_t GetRendererId() const override { return m_program; }

private:
    GLint GetUniformLocation(const std::string& name) {
        if (const auto found = m_uniformCache.find(name); found != m_uniformCache.end()) {
            return found->second;
        }

        const GLint location = glGetUniformLocation(m_program, name.c_str());
        if (location == -1) {
            XORA_LOG_WARN("Uniform '{}' was not found in shader '{}'", name, m_name);
        }
        m_uniformCache.emplace(name, location);
        return location;
    }

    std::string m_name;
    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_uniformCache;
};

std::unique_ptr<Shader> BuildShader(const std::string& name, const std::string& vertexSource,
                                    const std::string& fragmentSource) {
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertexSource, name + " vertex");
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, name + " fragment");
    const GLuint program = LinkProgram(vertex, fragment, name);
    return std::make_unique<OpenGLShader>(name, program);
}

} // namespace

std::unique_ptr<Shader> Shader::CreateFromFiles(const std::string& vertexPath,
                                                 const std::string& fragmentPath) {
    const std::string name = std::filesystem::path(vertexPath).stem().string();
    return BuildShader(name, ReadTextFile(vertexPath), ReadTextFile(fragmentPath));
}

std::unique_ptr<Shader> Shader::CreateFromSource(const std::string& name, const std::string& vertexSource,
                                                 const std::string& fragmentSource) {
    return BuildShader(name, vertexSource, fragmentSource);
}

} // namespace Xora2D