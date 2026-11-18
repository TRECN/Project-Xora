#pragma once

#include "Xora2D/Assets/AssetHandle.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Xora2D {

class AssetManager;

} // namespace Xora2D

namespace Xora2DEditor {

enum class ImportCategory : std::uint8_t { Texture, Audio, Script, Scene, Animation };

[[nodiscard]] const char* ImportCategoryLabel(ImportCategory category);

// Determines the import category from the file name. Returns nullopt (with a reason) for unsupported types,
// e.g. OGG (no Vorbis decoder in the engine) or Python scripts when scripting is compiled out.
[[nodiscard]] std::optional<ImportCategory> ClassifyImportSource(const std::filesystem::path& source,
                                                                 std::string& reason);

// Content validation without touching the project: decodes images and audio, parses YAML scenes and animation
// clips, and checks scripts are text. Safe to call from a worker thread.
[[nodiscard]] bool ValidateImportFile(const std::filesystem::path& file, ImportCategory category, std::string& error,
                                      std::string& warning);

// Destination folder for an import. Files go to the per-type default folder (Assets/Textures, Assets/Audio,
// Assets/Scripts, Assets/Animations, Scenes) unless the browser is currently inside that folder's subtree, in
// which case the current folder is used. This keeps the project layout clean.
[[nodiscard]] std::string ResolveImportDirectory(ImportCategory category, const std::string& currentDirectory);

// Picks "<stem>_<n><ext>" when `fileName` already exists in `absoluteDirectory`; compound extensions such as
// ".anim.yaml" are preserved.
[[nodiscard]] std::string MakeUniqueFileName(const std::filesystem::path& absoluteDirectory, const std::string& fileName);

[[nodiscard]] bool FilesHaveIdenticalContent(const std::filesystem::path& left, const std::filesystem::path& right);

// Result of the filesystem half of an import (validate + copy). Never touches the asset registry.
struct StagedImport {
    std::filesystem::path Source;
    ImportCategory Category = ImportCategory::Texture;
    bool Success = false;
    std::string Message;
    std::string Warning;
    std::string DestinationResourcePath;
    // True when the file was newly written by this import (and therefore must be removed if registration fails).
    bool CreatedFile = false;
    // True when an identical file already existed at the destination (duplicate import).
    bool ReusedExistingFile = false;
};

struct ContentImportItemResult {
    std::filesystem::path Source;
    bool Success = false;
    bool Duplicate = false;
    std::string Message;
    std::string Warning;
    Xora2D::AssetHandle Handle{};
    std::string DestinationResourcePath;
};

struct ContentImportBatchResult {
    std::vector<ContentImportItemResult> Items;
    [[nodiscard]] bool AllSucceeded() const;
    [[nodiscard]] std::size_t SuccessCount() const;
    [[nodiscard]] std::vector<std::string> AffectedDirectories() const;
};

// Validates `source` and copies it into the project via a hidden temp file + rename so a failed or interrupted
// copy never leaves a partial asset behind. Thread-safe with respect to the registry (does not use it).
[[nodiscard]] StagedImport StageImport(const std::filesystem::path& source, const std::string& currentDirectory);

// Registers a staged import with the AssetManager (main thread). Rolls back the copied file on failure.
[[nodiscard]] ContentImportItemResult CommitImport(const StagedImport& staged, Xora2D::AssetManager& assetManager);

// Synchronous convenience used by tests and small imports: stages and commits each file in dependency order
// (textures before animation clips that reference them).
[[nodiscard]] ContentImportBatchResult ImportExternalFiles(const std::vector<std::filesystem::path>& sources,
                                                           const std::string& currentDirectory,
                                                           Xora2D::AssetManager& assetManager);

// Runs the expensive half of imports (decode/validate/copy) on a worker thread. The main thread submits
// batches and polls for completion; registration happens during Poll() on the main thread.
class ContentImportQueue {
public:
    ContentImportQueue();
    ~ContentImportQueue();

    ContentImportQueue(const ContentImportQueue&) = delete;
    ContentImportQueue& operator=(const ContentImportQueue&) = delete;

    void Submit(std::vector<std::filesystem::path> sources, const std::string& currentDirectory);
    // Commits finished batches; returns at most one batch result per call.
    [[nodiscard]] std::optional<ContentImportBatchResult> Poll(Xora2D::AssetManager& assetManager);
    [[nodiscard]] bool IsBusy() const;
    [[nodiscard]] std::size_t GetPendingFileCount() const { return m_pendingFiles.load(); }
    // Blocks until all submitted work is staged (tests). Returns false on timeout.
    bool WaitUntilStaged(std::chrono::milliseconds timeout);
    void Shutdown();
    // Stops the worker, discards queued/staged work and starts a fresh worker (after a project switch).
    void Restart();

private:
    struct Job {
        std::vector<std::filesystem::path> Sources;
        std::string CurrentDirectory;
    };

    void WorkerLoop();

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<Job> m_jobs;
    std::deque<std::vector<StagedImport>> m_staged;
    std::atomic<std::size_t> m_pendingFiles{0};
    bool m_jobRunning = false;
    bool m_stop = false;
    std::thread m_worker;
};

} // namespace Xora2DEditor
