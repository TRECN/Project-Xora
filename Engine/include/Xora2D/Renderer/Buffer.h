#pragma once

#include <cstdint>
#include <memory>

namespace Xora2D {

class VertexBuffer {
public:
    virtual ~VertexBuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual void SetData(const void* data, std::uint32_t size, std::uint32_t offset = 0) = 0;
    virtual std::uint32_t GetSize() const = 0;

    static std::unique_ptr<VertexBuffer> Create(std::uint32_t size);
    static std::unique_ptr<VertexBuffer> CreateDynamic(std::uint32_t capacity);
};

class IndexBuffer {
public:
    virtual ~IndexBuffer() = default;

    virtual void Bind() const = 0;
    virtual void Unbind() const = 0;

    virtual std::uint32_t GetCount() const = 0;

    static std::unique_ptr<IndexBuffer> Create(const std::uint32_t* indices, std::uint32_t count);
};

} // namespace Xora2D