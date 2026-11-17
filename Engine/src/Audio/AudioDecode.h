#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Xora2D {

bool DecodeAudioFileToPcm(const std::filesystem::path& filepath, std::uint32_t& channels, std::uint32_t& sampleRate,
                          std::uint64_t& frameCount, std::shared_ptr<std::vector<float>>& pcm, std::string& error);

} // namespace Xora2D