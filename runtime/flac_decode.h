#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace edflac {
// Standalone Dreamcast music is strictly stereo,44100Hz,signed16 PCM.
std::vector<uint8_t> readMusic(const std::filesystem::path& path,size_t expectedPcmBytes);
}
