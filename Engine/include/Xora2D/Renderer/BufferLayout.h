#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Xora2D {

enum class ShaderDataType : std::uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4,
    Bool,
};

std::uint32_t ShaderDataTypeSize(ShaderDataType type);
std::uint32_t ShaderDataTypeComponentCount(ShaderDataType type);

class BufferElement {
public:
    BufferElement(ShaderDataType type, const std::string& name, bool normalized = false);

    ShaderDataType GetType() const { return m_type; }
    const std::string& GetName() const { return m_name; }
    std::uint32_t GetOffset() const { return m_offset; }
    std::uint32_t GetSize() const { return m_size; }
    bool IsNormalized() const { return m_normalized; }
    std::uint32_t GetComponentCount() const { return m_componentCount; }

    void SetOffset(std::uint32_t offset) { m_offset = offset; }

private:
    ShaderDataType m_type = ShaderDataType::Float;
    std::string m_name;
    std::uint32_t m_offset = 0;
    std::uint32_t m_size = 0;
    bool m_normalized = false;
    std::uint32_t m_componentCount = 0;
};

class BufferLayout {
public:
    BufferLayout() = default;
    explicit BufferLayout(std::initializer_list<BufferElement> elements);

    const std::vector<BufferElement>& GetElements() const { return m_elements; }
    std::uint32_t GetStride() const { return m_stride; }

private:
    void Recalculate();

    std::vector<BufferElement> m_elements;
    std::uint32_t m_stride = 0;
};

} // namespace Xora2D