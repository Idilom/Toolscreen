#include "video_media.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4267 4305)
#endif

#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kFallbackVideoFrameRate = 30.0;

std::string GetLowercaseExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }

    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

double SanitizeFrameRate(double frameRate) {
    if (frameRate > 1.0 && std::isfinite(frameRate)) {
        return frameRate;
    }
    return kFallbackVideoFrameRate;
}

bool TryComputeDecodedFrameBytes(int width, int height, size_t& outBytes) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixelCount > (std::numeric_limits<size_t>::max)() / 4u) {
        return false;
    }

    outBytes = pixelCount * 4u;
    return true;
}

bool WriteError(std::string* outError, const std::string& message) {
    if (outError) {
        *outError = message;
    }
    return false;
}

void FlipRgbaRowsInPlace(std::uint8_t* rgba, int width, int height, std::vector<std::uint8_t>& scratchRow) {
    if (!rgba || width <= 0 || height <= 1) {
        return;
    }

    const size_t stride = static_cast<size_t>(width) * 4u;
    if (scratchRow.size() < stride) {
        scratchRow.resize(stride);
    }

    for (int y = 0; y < height / 2; ++y) {
        const size_t topOffset = static_cast<size_t>(y) * stride;
        const size_t bottomOffset = static_cast<size_t>(height - 1 - y) * stride;
        std::copy_n(rgba + topOffset, stride, scratchRow.data());
        std::copy_n(rgba + bottomOffset, stride, rgba + topOffset);
        std::copy_n(scratchRow.data(), stride, rgba + bottomOffset);
    }
}

void FlipRgbaRowsInPlace(std::vector<std::uint8_t>& rgba, int width, int height, std::vector<std::uint8_t>& scratchRow) {
    if (width <= 0 || height <= 1) {
        return;
    }

    FlipRgbaRowsInPlace(rgba.data(), width, height, scratchRow);
}

bool ConvertFrameToRgbaBuffer(plm_frame_t* frame, int width, int height, std::uint8_t* outRgba,
                              std::vector<std::uint8_t>& scratchRow) {
    if (!frame || width <= 0 || height <= 0 || !outRgba) {
        return false;
    }

    const size_t frameBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    std::fill_n(outRgba, frameBytes, static_cast<std::uint8_t>(255));
    plm_frame_to_rgba(frame, outRgba, width * 4);
    FlipRgbaRowsInPlace(outRgba, width, height, scratchRow);
    return true;
}

} // namespace

VisualMediaKind DetectVisualMediaKindFromPath(const std::string& path) {
    const std::string extension = GetLowercaseExtension(path);
    if (extension == ".gif") {
        return VisualMediaKind::AnimatedGif;
    }
    if (extension == ".mpg" || extension == ".mpeg") {
        return VisualMediaKind::VideoMpeg1;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp") {
        return VisualMediaKind::StaticImage;
    }
    return VisualMediaKind::Unsupported;
}

bool IsSupportedVisualMediaPath(const std::string& path) { return DetectVisualMediaKindFromPath(path) != VisualMediaKind::Unsupported; }

const char* DescribeSupportedVisualMediaFormats() {
    return "Supported formats: PNG, JPG, JPEG, BMP, GIF, and MPEG-1 video (.mpg, .mpeg).";
}

bool ProbeMpegVideoFile(const std::string& pathUtf8, VideoProbeResult& outResult) {
    outResult = VideoProbeResult{};

    plm_t* decoder = plm_create_with_filename(pathUtf8.c_str());
    if (!decoder) {
        outResult.error = "Could not open MPEG-1 video file.";
        return false;
    }

    plm_set_audio_enabled(decoder, 0);
    plm_set_video_enabled(decoder, 1);
    plm_set_loop(decoder, 1);

    const int width = plm_get_width(decoder);
    const int height = plm_get_height(decoder);
    const double frameRate = SanitizeFrameRate(plm_get_framerate(decoder));
    const double durationSeconds = plm_get_duration(decoder);
    if (width <= 0 || height <= 0) {
        plm_destroy(decoder);
        outResult.error = "Invalid MPEG-1 video dimensions.";
        return false;
    }

    plm_frame_t* frame = plm_decode_video(decoder);
    if (!frame) {
        plm_destroy(decoder);
        outResult.error = "Could not decode the first MPEG-1 video frame.";
        return false;
    }

    std::vector<std::uint8_t> scratchRow;
    size_t firstFrameBytes = 0;
    if (!TryComputeDecodedFrameBytes(width, height, firstFrameBytes)) {
        plm_destroy(decoder);
        outResult.error = "Invalid MPEG-1 first-frame storage size.";
        return false;
    }
    outResult.firstFrameRgba.resize(firstFrameBytes);
    if (!ConvertFrameToRgbaBuffer(frame, width, height, outResult.firstFrameRgba.data(), scratchRow)) {
        plm_destroy(decoder);
        outResult.error = "Failed to convert the first MPEG-1 video frame to RGBA.";
        return false;
    }

    plm_destroy(decoder);

    outResult.success = true;
    outResult.width = width;
    outResult.height = height;
    outResult.frameRate = frameRate;
    outResult.durationSeconds = durationSeconds > 0.0 ? durationSeconds : 0.0;
    return true;
}

bool DecodeCachedMpegVideoFile(const std::string& pathUtf8, size_t maxDecodedBytes, CachedMpegVideoResult& outResult) {
    outResult = CachedMpegVideoResult{};

    plm_t* decoder = plm_create_with_filename(pathUtf8.c_str());
    if (!decoder) {
        outResult.error = "Could not open MPEG-1 video file.";
        return false;
    }

    plm_set_audio_enabled(decoder, 0);
    plm_set_video_enabled(decoder, 1);
    plm_set_loop(decoder, 0);

    const int width = plm_get_width(decoder);
    const int height = plm_get_height(decoder);
    const double frameRate = SanitizeFrameRate(plm_get_framerate(decoder));
    const double durationSeconds = plm_get_duration(decoder);
    if (width <= 0 || height <= 0) {
        plm_destroy(decoder);
        outResult.error = "Invalid MPEG-1 video dimensions.";
        return false;
    }

    size_t frameBytes = 0;
    if (!TryComputeDecodedFrameBytes(width, height, frameBytes) || frameBytes == 0) {
        plm_destroy(decoder);
        outResult.error = "Invalid MPEG-1 frame storage size.";
        return false;
    }
    if (frameBytes > maxDecodedBytes) {
        plm_destroy(decoder);
        outResult.error = "Decoded MPEG-1 frame size exceeds cache budget.";
        return false;
    }

    outResult.width = width;
    outResult.height = height;
    outResult.frameRate = frameRate;
    outResult.durationSeconds = durationSeconds > 0.0 ? durationSeconds : 0.0;

    std::vector<std::uint8_t> scratchRow;
    const int fallbackDelayMs = (std::max)(1, static_cast<int>(std::lround(1000.0 / frameRate)));
    int decodedFrames = 0;

    for (;;) {
        plm_frame_t* frame = plm_decode_video(decoder);
        if (!frame) {
            break;
        }

        const size_t previousSize = outResult.rgbaFrames.size();
        if (previousSize > maxDecodedBytes || frameBytes > (maxDecodedBytes - previousSize)) {
            plm_destroy(decoder);
            outResult = CachedMpegVideoResult{};
            outResult.error = "Decoded MPEG-1 video exceeds cache budget.";
            return false;
        }

        outResult.rgbaFrames.resize(previousSize + frameBytes);
        if (!ConvertFrameToRgbaBuffer(frame, width, height, outResult.rgbaFrames.data() + previousSize, scratchRow)) {
            plm_destroy(decoder);
            outResult = CachedMpegVideoResult{};
            outResult.error = "Failed to convert MPEG-1 frame to RGBA.";
            return false;
        }

        outResult.frameDelaysMs.push_back(fallbackDelayMs);
        decodedFrames++;
    }

    plm_destroy(decoder);

    if (decodedFrames <= 0 || outResult.rgbaFrames.empty()) {
        outResult = CachedMpegVideoResult{};
        outResult.error = "Could not decode any MPEG-1 video frames.";
        return false;
    }

    outResult.success = true;
    outResult.frameCount = decodedFrames;
    if (outResult.frameDelaysMs.size() < static_cast<size_t>(decodedFrames)) {
        outResult.frameDelaysMs.resize(static_cast<size_t>(decodedFrames), fallbackDelayMs);
    }
    return true;
}
