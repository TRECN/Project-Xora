#include "Xora2D/Renderer/BufferLayout.h"

#include <stdexcept>

namespace Xora2D {

std::uint32_t ShaderDataTypeSize(ShaderDataType type) {
  return ShaderDataTypeComponentCount(type) * sizeof(float);
}

std::uint32_t ShaderDataTypeComponentCount(ShaderDataType type) {
  switch (type) {
  case ShaderDataType::Float:
    return 1;
  case ShaderDataType::Float2:
    return 2;
  case ShaderDataType::Float3:
    return 3;
  case ShaderDataType::Float4:
    return 4;
  case ShaderDataType::Int:
    return 1;
  case ShaderDataType::Int2:
    return 2;
  case ShaderDataType::Int3:
    return 3;
  case ShaderDataType::Int4:
    return 4;
  case ShaderDataType::Bool:
    return 1;
  }
  throw std::runtime_error("Unknown shader data type");
}

BufferElement::BufferElement(ShaderDataType type, const std::string& name, bool normalized)
    : m_type(type), m_name(name), m_offset(0), m_size(ShaderDataTypeSize(type)), m_normalized(normalized),
      m_componentCount(ShaderDataTypeComponentCount(type)) {}

BufferLayout::BufferLayout(std::initializer_list<BufferElement> elements)
    : m_elements(elements) {
  Recalculate();
}

void BufferLayout::Recalculate() {
  std::uint32_t offset = 0;
  m_stride = 0;
  for (BufferElement& element : m_elements) {
    element.SetOffset(offset);
    offset += element.GetSize();
    m_stride = offset;
  }
}

} // namespace Xora2D