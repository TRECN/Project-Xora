#include "Xora2DEditor/Project/RecentProjects.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>

namespace Xora2DEditor {

RecentProjectList::RecentProjectList(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 1)) {}

std::string RecentProjectList::NormalizeKey(const std::filesystem::path& projectFile) {
    std::error_code errorCode;
    std::filesystem::path absolute = std::filesystem::absolute(projectFile, errorCode);
    if (errorCode) {
        absolute = projectFile;
    }
    // weakly_canonical resolves symlinks for paths that still exist and normalizes the rest.
    std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, errorCode);
    if (errorCode) {
        canonical = absolute.lexically_normal();
    }
    return canonical.generic_string();
}

std::int64_t RecentProjectList::NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void RecentProjectList::Add(const std::string& name, const std::filesystem::path& projectFile,
                            std::int64_t lastOpenedUnixSeconds) {
    const std::string key = NormalizeKey(projectFile);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&key](const RecentProjectEntry& entry) {
                                       return NormalizeKey(entry.ProjectFile) == key;
                                   }),
                    m_entries.end());
    RecentProjectEntry entry{};
    entry.Name = name;
    entry.ProjectFile = std::filesystem::path(key);
    entry.LastOpenedUnixSeconds = lastOpenedUnixSeconds;
    m_entries.insert(m_entries.begin(), std::move(entry));
    if (m_entries.size() > m_capacity) {
        m_entries.resize(m_capacity);
    }
}

bool RecentProjectList::Remove(const std::filesystem::path& projectFile) {
    const std::string key = NormalizeKey(projectFile);
    const auto oldSize = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&key](const RecentProjectEntry& entry) {
                                       return NormalizeKey(entry.ProjectFile) == key;
                                   }),
                    m_entries.end());
    return m_entries.size() != oldSize;
}

bool RecentProjectList::Contains(const std::filesystem::path& projectFile) const {
    const std::string key = NormalizeKey(projectFile);
    return std::any_of(m_entries.begin(), m_entries.end(), [&key](const RecentProjectEntry& entry) {
        return NormalizeKey(entry.ProjectFile) == key;
    });
}

bool RecentProjectList::IsAvailable(const RecentProjectEntry& entry) {
    std::error_code errorCode;
    return std::filesystem::is_regular_file(entry.ProjectFile, errorCode);
}

RecentProjectsResult RecentProjectList::Load(const std::filesystem::path& file) {
    RecentProjectsResult result{};
    m_entries.clear();
    std::error_code errorCode;
    if (!std::filesystem::exists(file, errorCode)) {
        result.Success = true;
        result.Message = "No recent projects yet";
        return result;
    }
    try {
        const YAML::Node root = YAML::LoadFile(file.string());
        const YAML::Node list = root["RecentProjects"];
        if (!list || !list.IsSequence()) {
            result.Message = "Recent projects file has no 'RecentProjects' list";
            return result;
        }
        // Stored newest first; Add() inserts at the front, so walk backwards to keep the order.
        for (std::size_t index = list.size(); index-- > 0;) {
            const YAML::Node item = list[index];
            if (!item.IsMap() || !item["Path"]) {
                continue;
            }
            const std::string path = item["Path"].as<std::string>("");
            if (path.empty()) {
                continue;
            }
            const std::string name = item["Name"] ? item["Name"].as<std::string>("") : std::string{};
            const std::int64_t opened = item["LastOpened"] ? item["LastOpened"].as<std::int64_t>(0) : 0;
            Add(name.empty() ? std::filesystem::path(path).stem().string() : name, path, opened);
        }
        result.Success = true;
        result.Message = "Loaded recent projects";
    } catch (const YAML::Exception& exception) {
        m_entries.clear();
        result.Message = std::string("Recent projects file is invalid: ") + exception.what();
    }
    return result;
}

RecentProjectsResult RecentProjectList::Save(const std::filesystem::path& file) const {
    RecentProjectsResult result{};
    std::error_code errorCode;
    std::filesystem::create_directories(file.parent_path(), errorCode);

    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "RecentProjects" << YAML::Value << YAML::BeginSeq;
    for (const RecentProjectEntry& entry : m_entries) {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << entry.Name;
        out << YAML::Key << "Path" << YAML::Value << entry.ProjectFile.generic_string();
        out << YAML::Key << "LastOpened" << YAML::Value << entry.LastOpenedUnixSeconds;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap;

    const std::filesystem::path temporary = file.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.Message = "Cannot write " + temporary.string();
            return result;
        }
        output << out.c_str() << "\n";
        if (!output) {
            result.Message = "Failed while writing " + temporary.string();
            return result;
        }
    }
    std::filesystem::rename(temporary, file, errorCode);
    if (errorCode) {
        std::filesystem::remove(temporary, errorCode);
        result.Message = "Cannot replace " + file.string();
        return result;
    }
    result.Success = true;
    result.Message = "Saved recent projects";
    return result;
}

} // namespace Xora2DEditor
