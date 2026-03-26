#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class VisualMediaKind {
    Unsupported,
    StaticImage,
    AnimatedGif,
    VideoMpeg1,
};

struct VideoProbeResult {
    bool success = false;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    double durationSeconds = 0.0;
    std::vector<std::uint8_t> firstFrameRgba;
    std::string error;
};

struct CachedMpegVideoResult {
    bool success = false;
    int width = 0;
    int height = 0;
    int frameCount = 0;
    double frameRate = 0.0;
    double durationSeconds = 0.0;
    std::vector<std::uint8_t> rgbaFrames;
    std::vector<int> frameDelaysMs;
    std::string error;
};

VisualMediaKind DetectVisualMediaKindFromPath(const std::string& path);
bool IsSupportedVisualMediaPath(const std::string& path);
const char* DescribeSupportedVisualMediaFormats();

bool ProbeMpegVideoFile(const std::string& pathUtf8, VideoProbeResult& outResult);
bool DecodeCachedMpegVideoFile(const std::string& pathUtf8, size_t maxDecodedBytes, CachedMpegVideoResult& outResult);