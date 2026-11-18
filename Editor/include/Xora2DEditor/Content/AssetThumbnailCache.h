#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Xora2D {

class Texture2D;

} // namespace Xora2D

namespace Xora2DEditor {

// CPU-side downscaled thumbnail (RGBA8, row 0 = top of the image).
struct ThumbnailImage {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t SourceWidth = 0;
    std::uint32_t SourceHeight = 0;
    std::vector<std::uint8_t> Pixels;
};

// Decodes `file` and box-filters it so the longest side is at most `maxSize`, preserving aspect ratio. Never
// upscales. Thread-safe; returns nullopt with `error` for unreadable/corrupt images.
[[nodiscard]] std::optional<ThumbnailImage> GenerateThumbnail(const std::filesystem::path& file, std::uint32_t maxSize,
                                                              std::string& error);

// Editor-only thumbnail cache. Images are decoded and downscaled on a worker thread (the full-resolution image
// is never kept); the small result is uploaded to the GPU on the main thread in ProcessCompleted(), a few per
// frame. Entries are keyed by resource-relative path and invalidated when the file's modification time changes.
class AssetThumbnailCache {
public:
    enum class State : std::uint8_t { Pending, Ready, Failed };

    struct View {
        State Status = State::Pending;
        std::uint32_t RendererId = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t SourceWidth = 0;
        std::uint32_t SourceHeight = 0;
        std::string Error;
    };

    using Uploader = std::function<std::unique_ptr<Xora2D::Texture2D>(const ThumbnailImage&)>;

    explicit AssetThumbnailCache(std::uint32_t thumbnailSize = 128, std::size_t capacity = 512);
    ~AssetThumbnailCache();

    AssetThumbnailCache(const AssetThumbnailCache&) = delete;
    AssetThumbnailCache& operator=(const AssetThumbnailCache&) = delete;

    // Defaults to Texture2D::CreateFromRGBA8 (needs a GL context). Tests inject a fake.
    void SetUploader(Uploader uploader) { m_uploader = std::move(uploader); }

    // Returns the current state and queues generation on a miss or when `lastModified` changed.
    View Request(const std::string& key, const std::filesystem::path& file,
                 std::filesystem::file_time_type lastModified);
    // Main thread: uploads up to `maxUploads` finished thumbnails. Returns how many were uploaded.
    std::size_t ProcessCompleted(std::size_t maxUploads = 4);

    void Invalidate(const std::string& key);
    // Invalidates `key` and all keys below it (folder rename/move/delete).
    void InvalidatePrefix(const std::string& keyPrefix);
    void Clear();
    void Shutdown();
    // Stops the worker, drops every entry and starts a fresh worker (project switch: old paths must not resolve).
    void Restart();

    [[nodiscard]] std::size_t GetGeneratedCount() const;
    [[nodiscard]] std::size_t GetEntryCount() const;
    // Tests: waits until the worker queue is empty. Returns false on timeout.
    bool WaitIdle(std::chrono::milliseconds timeout);

private:
    struct Entry {
        State Status = State::Pending;
        std::filesystem::file_time_type LastModified{};
        std::uint64_t Generation = 0;
        std::uint64_t LastUsed = 0;
        std::unique_ptr<Xora2D::Texture2D> Texture;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t SourceWidth = 0;
        std::uint32_t SourceHeight = 0;
        std::string Error;
    };
    struct Job {
        std::string Key;
        std::filesystem::path File;
        std::uint64_t Generation = 0;
    };
    struct Completed {
        std::string Key;
        std::uint64_t Generation = 0;
        std::optional<ThumbnailImage> Image;
        std::string Error;
    };

    void WorkerLoop();
    void EvictIfNeeded();

    std::uint32_t m_thumbnailSize;
    std::size_t m_capacity;
    Uploader m_uploader;

    // Main-thread state.
    std::unordered_map<std::string, Entry> m_entries;
    std::uint64_t m_nextGeneration = 1;
    std::uint64_t m_useCounter = 0;

    // Shared with the worker.
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Job> m_jobs;
    std::deque<Completed> m_completed;
    std::size_t m_generatedCount = 0;
    bool m_workerBusy = false;
    bool m_stop = false;
    std::thread m_worker;
};

} // namespace Xora2DEditor
