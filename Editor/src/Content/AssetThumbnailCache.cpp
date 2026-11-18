#include "Xora2DEditor/Content/AssetThumbnailCache.h"

#include "Xora2D/Renderer/Texture2D.h"

#include <stb_image.h>

#include <algorithm>

namespace Xora2DEditor {

std::optional<ThumbnailImage> GenerateThumbnail(const std::filesystem::path& file, std::uint32_t maxSize,
                                                std::string& error) {
    if (maxSize == 0) {
        error = "Invalid thumbnail size";
        return std::nullopt;
    }
    // The flip flag is global in stb_image; the per-thread override keeps worker decodes deterministic even while
    // the main thread loads textures with flipping enabled.
    stbi_set_flip_vertically_on_load_thread(0);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        error = std::string("Cannot decode image: ") + stbi_failure_reason();
        return std::nullopt;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        error = "Image has no pixels";
        return std::nullopt;
    }

    ThumbnailImage image{};
    image.SourceWidth = static_cast<std::uint32_t>(width);
    image.SourceHeight = static_cast<std::uint32_t>(height);
    const float scale = std::min(1.0f, static_cast<float>(maxSize) / static_cast<float>(std::max(width, height)));
    image.Width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(static_cast<float>(width) * scale + 0.5f));
    image.Height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(static_cast<float>(height) * scale + 0.5f));
    image.Pixels.resize(static_cast<std::size_t>(image.Width) * image.Height * 4);

    // Box filter: average all source pixels that map onto each destination pixel.
    for (std::uint32_t y = 0; y < image.Height; ++y) {
        const std::uint32_t sourceY0 = y * image.SourceHeight / image.Height;
        const std::uint32_t sourceY1 = std::max(sourceY0 + 1, (y + 1) * image.SourceHeight / image.Height);
        for (std::uint32_t x = 0; x < image.Width; ++x) {
            const std::uint32_t sourceX0 = x * image.SourceWidth / image.Width;
            const std::uint32_t sourceX1 = std::max(sourceX0 + 1, (x + 1) * image.SourceWidth / image.Width);
            std::uint32_t sum[4] = {0, 0, 0, 0};
            std::uint32_t count = 0;
            for (std::uint32_t sy = sourceY0; sy < sourceY1; ++sy) {
                const stbi_uc* row = pixels + (static_cast<std::size_t>(sy) * image.SourceWidth) * 4;
                for (std::uint32_t sx = sourceX0; sx < sourceX1; ++sx) {
                    for (int channel = 0; channel < 4; ++channel) {
                        sum[channel] += row[sx * 4 + static_cast<std::uint32_t>(channel)];
                    }
                    ++count;
                }
            }
            std::uint8_t* destination = image.Pixels.data() + (static_cast<std::size_t>(y) * image.Width + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                destination[channel] = static_cast<std::uint8_t>(sum[channel] / std::max(count, 1u));
            }
        }
    }
    stbi_image_free(pixels);
    return image;
}

AssetThumbnailCache::AssetThumbnailCache(std::uint32_t thumbnailSize, std::size_t capacity)
    : m_thumbnailSize(thumbnailSize),
      m_capacity(std::max<std::size_t>(capacity, 1)),
      m_uploader([](const ThumbnailImage& image) {
          return Xora2D::Texture2D::CreateFromRGBA8(image.Width, image.Height, image.Pixels.data());
      }),
      m_worker([this]() { WorkerLoop(); }) {}

AssetThumbnailCache::~AssetThumbnailCache() {
    Shutdown();
}

void AssetThumbnailCache::Shutdown() {
    {
        std::scoped_lock lock(m_mutex);
        if (m_stop) {
            return;
        }
        m_stop = true;
        m_jobs.clear();
    }
    m_condition.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_completed.clear();
}

void AssetThumbnailCache::Restart() {
    Shutdown();
    Clear();
    {
        std::scoped_lock lock(m_mutex);
        m_jobs.clear();
        m_completed.clear();
        m_workerBusy = false;
        m_stop = false;
    }
    m_worker = std::thread([this]() { WorkerLoop(); });
}

AssetThumbnailCache::View AssetThumbnailCache::Request(const std::string& key, const std::filesystem::path& file,
                                                       std::filesystem::file_time_type lastModified) {
    auto found = m_entries.find(key);
    if (found == m_entries.end() || found->second.LastModified != lastModified) {
        Entry entry{};
        entry.LastModified = lastModified;
        entry.Generation = m_nextGeneration++;
        entry.LastUsed = ++m_useCounter; // newest entry is never the eviction victim
        {
            std::scoped_lock lock(m_mutex);
            if (!m_stop) {
                m_jobs.push_back(Job{key, file, entry.Generation});
            }
        }
        m_condition.notify_one();
        found = m_entries.insert_or_assign(key, std::move(entry)).first;
        EvictIfNeeded();
        found = m_entries.find(key);
    }
    Entry& entry = found->second;
    entry.LastUsed = ++m_useCounter;

    View view{};
    view.Status = entry.Status;
    view.RendererId = entry.Texture ? entry.Texture->GetRendererId() : 0;
    view.Width = entry.Width;
    view.Height = entry.Height;
    view.SourceWidth = entry.SourceWidth;
    view.SourceHeight = entry.SourceHeight;
    view.Error = entry.Error;
    return view;
}

std::size_t AssetThumbnailCache::ProcessCompleted(std::size_t maxUploads) {
    std::size_t uploaded = 0;
    while (uploaded < maxUploads) {
        Completed completed;
        {
            std::scoped_lock lock(m_mutex);
            if (m_completed.empty()) {
                break;
            }
            completed = std::move(m_completed.front());
            m_completed.pop_front();
        }
        const auto found = m_entries.find(completed.Key);
        // Results for invalidated/replaced entries are discarded.
        if (found == m_entries.end() || found->second.Generation != completed.Generation) {
            continue;
        }
        Entry& entry = found->second;
        if (!completed.Image.has_value()) {
            entry.Status = State::Failed;
            entry.Error = completed.Error;
            continue;
        }
        entry.Width = completed.Image->Width;
        entry.Height = completed.Image->Height;
        entry.SourceWidth = completed.Image->SourceWidth;
        entry.SourceHeight = completed.Image->SourceHeight;
        entry.Texture = m_uploader ? m_uploader(*completed.Image) : nullptr;
        entry.Status = entry.Texture ? State::Ready : State::Failed;
        if (!entry.Texture) {
            entry.Error = "GPU upload failed";
        }
        ++uploaded;
    }
    return uploaded;
}

void AssetThumbnailCache::Invalidate(const std::string& key) {
    m_entries.erase(key);
}

void AssetThumbnailCache::InvalidatePrefix(const std::string& keyPrefix) {
    std::erase_if(m_entries, [&](const auto& item) {
        const std::string& key = item.first;
        return key == keyPrefix || (key.starts_with(keyPrefix) && key.size() > keyPrefix.size() &&
                                    key[keyPrefix.size()] == '/');
    });
}

void AssetThumbnailCache::Clear() {
    m_entries.clear();
    std::scoped_lock lock(m_mutex);
    m_jobs.clear();
}

std::size_t AssetThumbnailCache::GetGeneratedCount() const {
    std::scoped_lock lock(m_mutex);
    return m_generatedCount;
}

std::size_t AssetThumbnailCache::GetEntryCount() const {
    return m_entries.size();
}

bool AssetThumbnailCache::WaitIdle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    return m_condition.wait_for(lock, timeout, [this]() { return m_stop || (m_jobs.empty() && !m_workerBusy); });
}

void AssetThumbnailCache::EvictIfNeeded() {
    while (m_entries.size() > m_capacity) {
        auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const auto& left, const auto& right) {
            return left.second.LastUsed < right.second.LastUsed;
        });
        m_entries.erase(oldest);
    }
}

void AssetThumbnailCache::WorkerLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this]() { return m_stop || !m_jobs.empty(); });
            if (m_stop) {
                return;
            }
            // Most recently requested first: whatever the user is looking at now.
            job = std::move(m_jobs.back());
            m_jobs.pop_back();
            m_workerBusy = true;
        }
        Completed completed{};
        completed.Key = job.Key;
        completed.Generation = job.Generation;
        completed.Image = GenerateThumbnail(job.File, m_thumbnailSize, completed.Error);
        {
            std::scoped_lock lock(m_mutex);
            m_completed.push_back(std::move(completed));
            ++m_generatedCount;
            m_workerBusy = false;
        }
        m_condition.notify_all();
    }
}

} // namespace Xora2DEditor
