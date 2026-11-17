#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Xora2D {

class Shader {
public:
    static std::unique_ptr<Shader> CreateFromFiles(const std::string& vertexPath,
                                                    const std::string& fragmentPath);
    static std::unique_ptr<Shader> CreateFromSource(const std::string& name, const std::string& vertexSource,
                                                    const std::string& fragmentSource);

    virtual ~Shader() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual void SetInt(const std::string& name, int value) = 0;
    virtual void SetFloat(const std::string& name, float value) = 0;
    virtual void SetVec2(const std::string& name, const glm::vec2& value) = 0;
    virtual void SetVec3(const std::string& name, const glm::vec3& value) = 0;
    virtual void SetVec4(const std::string& name, const glm::vec4& value) = 0;
    virtual void SetMat3(const std::string& name, const glm::mat3& value) = 0;
    virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

    virtual const std::string& GetName() const = 0;
    virtual std::uint32_t GetRendererId() const = 0;
};

} // namespace Xora2D