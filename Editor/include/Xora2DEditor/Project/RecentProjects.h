#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Xora2DEditor {

struct RecentProjectEntry {
    std::string Name;
    std::filesystem::path ProjectFile;
    // Seconds since the Unix epoch; 0 when unknown.
    std::int64_t LastOpenedUnixSeconds = 0;
};

struct RecentProjectsResult {
    bool Success = false;
    std::string Message;
};

// Most-recently-opened projects, newest first. Stored in the application config directory (never inside a project).
// Entries are keyed by their normalized project-file path, so the same project is never listed twice. Removing an
// entry only edits this list; project files are never touched.
class RecentProjectList {
public:
    static constexpr std::size_t kDefaultCapacity = 10;

    explicit RecentProjectList(std::size_t capacity = kDefaultCapacity);

    // Inserts or moves the project to the front and refreshes its name / timestamp; trims to capacity.
    void Add(const std::string& name, const std::filesystem::path& projectFile, std::int64_t lastOpenedUnixSeconds);
    bool Remove(const std::filesystem::path& projectFile);
    [[nodiscard]] bool Contains(const std::filesystem::path& projectFile) const;
    void Clear() { m_entries.clear(); }

    [[nodiscard]] const std::vector<RecentProjectEntry>& GetEntries() const { return m_entries; }
    [[nodiscard]] std::size_t GetCapacity() const { return m_capacity; }

    // A missing or unreadable file yields an empty list (Success is false only for unreadable/invalid files).
    RecentProjectsResult Load(const std::filesystem::path& file);
    RecentProjectsResult Save(const std::filesystem::path& file) const;

    // True when the project file still exists (projects can be moved or deleted outside the editor).
    [[nodiscard]] static bool IsAvailable(const RecentProjectEntry& entry);
    // Absolute, lexically normal form used for duplicate detection.
    [[nodiscard]] static std::string NormalizeKey(const std::filesystem::path& projectFile);

    [[nodiscard]] static std::int64_t NowUnixSeconds();

private:
    std::size_t m_capacity;
    std::vector<RecentProjectEntry> m_entries;
};

} // namespace Xora2DEditor
