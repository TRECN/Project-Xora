#include "Xora2DEditor/Content/ContentBrowserImport.h"

#include "Xora2DEditor/Content/ContentBrowserPaths.h"

#include "Xora2D/Animation/AnimationClipAsset.h"
#include "Xora2D/Assets/AssetManager.h"
#include "Xora2D/Assets/AssetPaths.h"
#include "Xora2D/Audio/AudioClip.h"

#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

namespace Xora2DEditor {
namespace {

constexpr std::uintmax_t kMaxImportBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kMaxScriptBytes = 4ULL * 1024ULL * 1024ULL;

std::string DefaultDirectoryFor(ImportCategory category) {
    switch (category) {
    case ImportCategory::Texture:
        return "Assets/Textures";
    case ImportCategory::Audio:
        return "Assets/Audio";
    case ImportCategory::Script:
        return "Assets/Scripts";
    case ImportCategory::Animation:
        return "Assets/Animations";
    case ImportCategory::Scene:
        return std::string(kScenesFolder);
    }
    return std::string(kAssetsFolder);
}

std::string RandomSuffix() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    std::ostringstream stream;
    stream << std::hex << generator();
    return stream.str();
}

bool ValidateTexture(const std::filesystem::path& file, std::string& error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string path = file.string();
    if (stbi_info(path.c_str(), &width, &height, &channels) == 0) {
        error = std::string("Not a readable image: ") + stbi_failure_reason();
        return false;
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        error = "Image dimensions are outside the supported range (1-16384)";
        return false;
    }
    // Full decode catches truncated/corrupt pixel data that a header check would miss.
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (pixels == nullptr) {
        error = std::string("Image data is corrupt: ") + stbi_failure_reason();
        return false;
    }
    stbi_image_free(pixels);
    if (channels != 3 && channels != 4) {
        error = "Only RGB and RGBA images are supported by the renderer";
        return false;
    }
    return true;
}

bool ValidateAudio(const std::filesystem::path& file, std::string& error) {
    std::shared_ptr<Xora2D::AudioClip> clip;
    const Xora2D::AudioLoadResult result = Xora2D::AudioClip::LoadFromFile(file, clip);
    if (!result.Success || clip == nullptr || !clip->IsValid()) {
        error = "Audio could not be decoded: " + (result.Message.empty() ? std::string("unknown error") : result.Message);
        return false;
    }
    if (clip->GetFrameCount() == 0) {
        error = "Audio file contains no samples";
        return false;
    }
    return true;
}

bool ValidateScript(const std::filesystem::path& file, std::string& error) {
    std::error_code errorCode;
    if (std::filesystem::file_size(file, errorCode) > kMaxScriptBytes) {
        error = "Script is larger than 4 MB";
        return false;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "Script cannot be opened";
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contents.find('\0') != std::string::npos) {
        error = "Script appears to be a binary file";
        return false;
    }
    return true;
}

bool LoadYaml(const std::filesystem::path& file, YAML::Node& root, std::string& error) {
    try {
        root = YAML::LoadFile(file.string());
    } catch (const YAML::Exception& exception) {
        error = std::string("Invalid YAML: ") + exception.what();
        return false;
    }
    if (!root.IsMap()) {
        error = "YAML root must be a map";
        return false;
    }
    return true;
}

bool ValidateScene(const std::filesystem::path& file, std::string& error) {
    YAML::Node root;
    if (!LoadYaml(file, root, error)) {
        return false;
    }
    const YAML::Node entities = root["Entities"];
    if (!entities) {
        error = "Not a Xora2D scene: missing 'Entities'";
        return false;
    }
    if (!entities.IsSequence() && !entities.IsNull()) {
        error = "Scene 'Entities' must be a sequence";
        return false;
    }
    return true;
}

bool ValidateAnimation(const std::filesystem::path& file, std::string& error, std::string& warning) {
    YAML::Node root;
    if (!LoadYaml(file, root, error)) {
        return false;
    }
    try {
        if (!root["Version"] || root["Version"].as<int>() != 1) {
            error = "Unsupported or missing animation clip Version (expected 1)";
            return false;
        }
    } catch (const YAML::Exception&) {
        error = "Animation clip Version must be an integer";
        return false;
    }
    if (!root["Frames"] && !root["SpriteSheet"]) {
        error = "Animation clip needs 'Frames' or 'SpriteSheet'";
        return false;
    }
    // Full load: may legitimately fail only because the referenced texture is not in this project yet.
    Xora2D::AnimationClipAsset clip;
    const Xora2D::AssetResult load = Xora2D::AnimationClipAsset::LoadFromFile(file, file.filename().string(), clip);
    if (!load.Success) {
        if (load.Message.find("texture") != std::string::npos || load.Message.find("Texture") != std::string::npos) {
            warning = "Clip imported, but its texture is not in the project yet: " + load.Message;
            return true;
        }
        error = load.Message;
        return false;
    }
    return true;
}

int CategoryOrder(ImportCategory category) {
    // Textures first so animation clips imported in the same batch can resolve them.
    switch (category) {
    case ImportCategory::Texture:
        return 0;
    case ImportCategory::Audio:
        return 1;
    case ImportCategory::Script:
        return 2;
    case ImportCategory::Animation:
        return 3;
    case ImportCategory::Scene:
        return 4;
    }
    return 5;
}

std::vector<std::filesystem::path> OrderForImport(std::vector<std::filesystem::path> sources) {
    std::stable_sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        std::string ignored;
        const auto a = ClassifyImportSource(left, ignored);
        const auto b = ClassifyImportSource(right, ignored);
        return (a ? CategoryOrder(*a) : 9) < (b ? CategoryOrder(*b) : 9);
    });
    return sources;
}

} // namespace

const char* ImportCategoryLabel(ImportCategory category) {
    switch (category) {
    case ImportCategory::Texture:
        return "Texture";
    case ImportCategory::Audio:
        return "Audio";
    case ImportCategory::Script:
        return "Python Script";
    case ImportCategory::Scene:
        return "Scene";
    case ImportCategory::Animation:
        return "Animation Clip";
    }
    return "?";
}

std::optional<ImportCategory> ClassifyImportSource(const std::filesystem::path& source, std::string& reason) {
    const std::string extension = FullExtensionLower(source);
    if (extension == ".prefab.yaml") {
        reason = "Prefabs reference project asset handles; create them in the project (Hierarchy > Create Prefab)";
        return std::nullopt;
    }
    if (extension == ".scene.yaml") {
        return ImportCategory::Scene;
    }
    if (extension == ".anim.yaml") {
        return ImportCategory::Animation;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
        return ImportCategory::Texture;
    }
    if (extension == ".wav" || extension == ".mp3") {
        return ImportCategory::Audio;
    }
    if (extension == ".ogg") {
        reason = "OGG Vorbis is not supported: the engine's audio decoder handles WAV and MP3 only";
        return std::nullopt;
    }
    if (extension == ".py") {
#if XORA2D_SCRIPTING_ENABLED
        return ImportCategory::Script;
#else
        reason = "Python scripts cannot be imported: this build has scripting disabled";
        return std::nullopt;
#endif
    }
    if (extension == ".yaml" || extension == ".yml") {
        reason = "Only '.scene.yaml' scenes and '.anim.yaml' animation clips can be imported";
        return std::nullopt;
    }
    reason = "Unsupported file type '" + (extension.empty() ? std::string("(none)") : extension) + "'";
    return std::nullopt;
}

bool ValidateImportFile(const std::filesystem::path& file, ImportCategory category, std::string& error,
                        std::string& warning) {
    switch (category) {
    case ImportCategory::Texture:
        return ValidateTexture(file, error);
    case ImportCategory::Audio:
        return ValidateAudio(file, error);
    case ImportCategory::Script:
        return ValidateScript(file, error);
    case ImportCategory::Scene:
        return ValidateScene(file, error);
    case ImportCategory::Animation:
        return ValidateAnimation(file, error, warning);
    }
    error = "Unknown import category";
    return false;
}

std::string ResolveImportDirectory(ImportCategory category, const std::string& currentDirectory) {
    const std::string defaultDirectory = DefaultDirectoryFor(category);
    const std::optional<std::string> current = NormalizeResourceRelativePath(currentDirectory);
    if (current.has_value() && IsResourcePathWithin(*current, defaultDirectory) &&
        !IsResourcePathWithin(*current, kRegistryFolder)) {
        return *current;
    }
    return defaultDirectory;
}

std::string MakeUniqueFileName(const std::filesystem::path& absoluteDirectory, const std::string& fileName) {
    std::error_code errorCode;
    if (!std::filesystem::exists(absoluteDirectory / fileName, errorCode)) {
        return fileName;
    }
    std::string extension = FullExtensionLower(fileName);
    // Preserve the original casing of the extension.
    extension = fileName.substr(fileName.size() - extension.size());
    const std::string stem = fileName.substr(0, fileName.size() - extension.size());
    for (int index = 1; index < 10000; ++index) {
        const std::string candidate = stem + "_" + std::to_string(index) + extension;
        if (!std::filesystem::exists(absoluteDirectory / candidate, errorCode)) {
            return candidate;
        }
    }
    return stem + "_" + RandomSuffix() + extension;
}

bool FilesHaveIdenticalContent(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code errorCode;
    const auto leftSize = std::filesystem::file_size(left, errorCode);
    if (errorCode) {
        return false;
    }
    const auto rightSize = std::filesystem::file_size(right, errorCode);
    if (errorCode || leftSize != rightSize) {
        return false;
    }
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) {
        return false;
    }
    std::array<char, 64 * 1024> bufferA{};
    std::array<char, 64 * 1024> bufferB{};
    while (a && b) {
        a.read(bufferA.data(), bufferA.size());
        b.read(bufferB.data(), bufferB.size());
        const std::streamsize readA = a.gcount();
        if (readA != b.gcount() ||
            !std::equal(bufferA.begin(), bufferA.begin() + readA, bufferB.begin())) {
            return false;
        }
    }
    return true;
}

bool ContentImportBatchResult::AllSucceeded() const {
    return std::all_of(Items.begin(), Items.end(), [](const ContentImportItemResult& item) { return item.Success; });
}

std::size_t ContentImportBatchResult::SuccessCount() const {
    return static_cast<std::size_t>(
        std::count_if(Items.begin(), Items.end(), [](const ContentImportItemResult& item) { return item.Success; }));
}

std::vector<std::string> ContentImportBatchResult::AffectedDirectories() const {
    std::set<std::string> directories;
    for (const ContentImportItemResult& item : Items) {
        if (!item.DestinationResourcePath.empty()) {
            std::string directory = ParentResourceDirectory(item.DestinationResourcePath);
            // Newly created folders change their parents' listings too.
            while (true) {
                directories.insert(directory);
                if (directory.empty()) {
                    break;
                }
                directory = ParentResourceDirectory(directory);
            }
        }
    }
    return {directories.begin(), directories.end()};
}

StagedImport StageImport(const std::filesystem::path& source, const std::string& currentDirectory) {
    StagedImport staged{};
    staged.Source = source;

    std::string reason;
    const std::optional<ImportCategory> category = ClassifyImportSource(source, reason);
    if (!category.has_value()) {
        staged.Message = reason;
        return staged;
    }
    staged.Category = *category;

    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(source, errorCode)) {
        staged.Message = "Source is not a readable file";
        return staged;
    }
    const std::uintmax_t size = std::filesystem::file_size(source, errorCode);
    if (errorCode || size == 0) {
        staged.Message = "Source file is empty or unreadable";
        return staged;
    }
    if (size > kMaxImportBytes) {
        staged.Message = "Source file is larger than 512 MB";
        return staged;
    }

    if (!ValidateImportFile(source, *category, staged.Message, staged.Warning)) {
        return staged;
    }

    // A file that already lives inside the project is registered in place rather than duplicated.
    if (const std::optional<std::string> inProject = AbsoluteToResourceRelative(source)) {
        const bool managedLocation = *category == ImportCategory::Scene
                                         ? IsResourcePathWithin(*inProject, kScenesFolder)
                                         : AssetRelativePathFromResourcePath(*inProject).has_value();
        if (managedLocation && !IsResourcePathWithin(*inProject, kRegistryFolder)) {
            staged.Success = true;
            staged.ReusedExistingFile = true;
            staged.DestinationResourcePath = *inProject;
            staged.Message = "File is already inside the project";
            return staged;
        }
    }

    const std::string directory = ResolveImportDirectory(*category, currentDirectory);
    const std::filesystem::path absoluteDirectory = ResolveResourceRelativePath(directory);
    if (absoluteDirectory.empty()) {
        staged.Message = "Import folder is outside the project";
        return staged;
    }
    std::filesystem::create_directories(absoluteDirectory, errorCode);
    if (errorCode) {
        staged.Message = "Could not create folder '" + directory + "': " + errorCode.message();
        return staged;
    }

    const std::string originalName = source.filename().string();
    if (!IsValidEntryName(originalName)) {
        staged.Message = "File name '" + originalName + "' contains unsupported characters";
        return staged;
    }

    // Duplicate detection: identical bytes at the destination means "already imported".
    const std::filesystem::path existing = absoluteDirectory / originalName;
    if (std::filesystem::exists(existing, errorCode) && FilesHaveIdenticalContent(source, existing)) {
        staged.Success = true;
        staged.ReusedExistingFile = true;
        staged.DestinationResourcePath = JoinResourcePath(directory, originalName);
        staged.Message = "Identical file already in project";
        return staged;
    }

    const std::string finalName = MakeUniqueFileName(absoluteDirectory, originalName);
    const std::filesystem::path temporary =
        absoluteDirectory / (std::string(kImportTempPrefix) + RandomSuffix() + ".tmp");
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::none, errorCode);
    if (errorCode) {
        std::filesystem::remove(temporary, errorCode);
        staged.Message = "Copy failed: " + errorCode.message();
        return staged;
    }
    if (std::filesystem::file_size(temporary, errorCode) != size || errorCode) {
        std::filesystem::remove(temporary, errorCode);
        staged.Message = "Copy was incomplete";
        return staged;
    }
    const std::filesystem::path destination = absoluteDirectory / finalName;
    if (std::filesystem::exists(destination, errorCode)) {
        std::filesystem::remove(temporary, errorCode);
        staged.Message = "Destination appeared during import; try again";
        return staged;
    }
    std::filesystem::rename(temporary, destination, errorCode);
    if (errorCode) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        staged.Message = "Could not finalize copied file: " + errorCode.message();
        return staged;
    }

    staged.Success = true;
    staged.CreatedFile = true;
    staged.DestinationResourcePath = JoinResourcePath(directory, finalName);
    staged.Message = finalName == originalName ? "Imported" : "Imported as '" + finalName + "' (name was taken)";
    return staged;
}

ContentImportItemResult CommitImport(const StagedImport& staged, Xora2D::AssetManager& assetManager) {
    ContentImportItemResult result{};
    result.Source = staged.Source;
    result.Message = staged.Message;
    result.Warning = staged.Warning;
    result.DestinationResourcePath = staged.DestinationResourcePath;
    if (!staged.Success) {
        return result;
    }

    auto rollback = [&](const std::string& message) {
        if (staged.CreatedFile) {
            std::error_code errorCode;
            std::filesystem::remove(ResolveResourceRelativePath(staged.DestinationResourcePath), errorCode);
        }
        result.Success = false;
        result.Message = message;
        result.DestinationResourcePath.clear();
        return result;
    };

    if (staged.Category == ImportCategory::Scene) {
        result.Success = true;
        result.Duplicate = staged.ReusedExistingFile;
        return result;
    }

    const std::optional<std::string> assetRelative = AssetRelativePathFromResourcePath(staged.DestinationResourcePath);
    if (!assetRelative.has_value()) {
        return rollback("Imported file is not under Assets/");
    }

    const bool alreadyRegistered = assetManager.GetRegistry().FindByRelativePath(*assetRelative) != nullptr;
    const std::string displayName = std::filesystem::path(FileNameOf(*assetRelative)).stem().string();
    Xora2D::AssetResult registration{};
    switch (staged.Category) {
    case ImportCategory::Texture:
        registration = assetManager.RegisterTextureAtRelativePath(*assetRelative, displayName);
        break;
    case ImportCategory::Audio:
        registration = assetManager.RegisterAudioAtRelativePath(*assetRelative, displayName);
        break;
    case ImportCategory::Script:
        registration = assetManager.RegisterPythonScriptAtRelativePath(*assetRelative, displayName);
        break;
    case ImportCategory::Animation:
        registration = assetManager.RegisterAnimationClipAtRelativePath(
            *assetRelative, std::filesystem::path(displayName).stem().string());
        break;
    case ImportCategory::Scene:
        break;
    }
    if (!registration.Success) {
        return rollback("Registration failed: " + registration.Message);
    }

    result.Success = true;
    result.Handle = registration.Handle;
    result.Duplicate = alreadyRegistered;
    if (alreadyRegistered) {
        result.Message = "Already imported (existing handle reused)";
    }
    return result;
}

ContentImportBatchResult ImportExternalFiles(const std::vector<std::filesystem::path>& sources,
                                             const std::string& currentDirectory, Xora2D::AssetManager& assetManager) {
    ContentImportBatchResult batch{};
    for (const std::filesystem::path& source : OrderForImport(sources)) {
        batch.Items.push_back(CommitImport(StageImport(source, currentDirectory), assetManager));
    }
    return batch;
}

ContentImportQueue::ContentImportQueue()
    : m_worker([this]() { WorkerLoop(); }) {}

ContentImportQueue::~ContentImportQueue() {
    Shutdown();
}

void ContentImportQueue::Shutdown() {
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
}

void ContentImportQueue::Restart() {
    Shutdown();
    {
        std::scoped_lock lock(m_mutex);
        m_jobs.clear();
        m_staged.clear();
        m_pendingFiles = 0;
        m_jobRunning = false;
        m_stop = false;
    }
    m_worker = std::thread([this]() { WorkerLoop(); });
}

void ContentImportQueue::Submit(std::vector<std::filesystem::path> sources, const std::string& currentDirectory) {
    if (sources.empty()) {
        return;
    }
    {
        std::scoped_lock lock(m_mutex);
        if (m_stop) {
            return;
        }
        m_pendingFiles += sources.size();
        m_jobs.push_back(Job{OrderForImport(std::move(sources)), currentDirectory});
    }
    m_condition.notify_all();
}

std::optional<ContentImportBatchResult> ContentImportQueue::Poll(Xora2D::AssetManager& assetManager) {
    std::vector<StagedImport> staged;
    {
        std::scoped_lock lock(m_mutex);
        if (m_staged.empty()) {
            return std::nullopt;
        }
        staged = std::move(m_staged.front());
        m_staged.pop_front();
    }
    ContentImportBatchResult batch{};
    for (const StagedImport& item : staged) {
        batch.Items.push_back(CommitImport(item, assetManager));
    }
    return batch;
}

bool ContentImportQueue::IsBusy() const {
    std::scoped_lock lock(m_mutex);
    return m_jobRunning || !m_jobs.empty() || !m_staged.empty();
}

bool ContentImportQueue::WaitUntilStaged(std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    return m_condition.wait_for(lock, timeout, [this]() { return m_stop || (!m_jobRunning && m_jobs.empty()); });
}

void ContentImportQueue::WorkerLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this]() { return m_stop || !m_jobs.empty(); });
            if (m_stop) {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_jobRunning = true;
        }
        std::vector<StagedImport> staged;
        staged.reserve(job.Sources.size());
        for (const std::filesystem::path& source : job.Sources) {
            {
                std::scoped_lock lock(m_mutex);
                if (m_stop) {
                    break;
                }
            }
            staged.push_back(StageImport(source, job.CurrentDirectory));
            --m_pendingFiles;
        }
        {
            std::scoped_lock lock(m_mutex);
            m_staged.push_back(std::move(staged));
            m_jobRunning = false;
        }
        m_condition.notify_all();
    }
}

} // namespace Xora2DEditor
