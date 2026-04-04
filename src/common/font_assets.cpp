#include "font_assets.h"

#include "common/utils.h"
#include "platform/resource.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace {

std::filesystem::path Utf8Path(std::string_view path) {
    return std::filesystem::path(Utf8ToWide(std::string(path)));
}

std::string NormalizePathForComparison(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::string normalized = path;
    try {
        normalized = WideToUtf8(Utf8Path(path).lexically_normal().wstring());
    } catch (const std::exception&) {
        normalized = path;
    }

    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '\\') {
        normalized.erase(0, 2);
    }
    while (normalized.size() > 1 && normalized.back() == '\\') {
        normalized.pop_back();
    }

    return normalized;
}

bool PathsEqualIgnoreCase(const std::string& left, const std::string& right) {
    return EqualsIgnoreCase(NormalizePathForComparison(left), NormalizePathForComparison(right));
}

std::string BuildAbsoluteBundledFontPath(const std::filesystem::path& rootPath, const BundledFontAsset& asset) {
    return WideToUtf8((rootPath / Utf8Path(asset.relativePath)).wstring());
}

bool MatchesBundledFontPath(const BundledFontAsset& asset, const std::string& path, const std::wstring& toolscreenPath) {
    if (path.empty()) {
        return false;
    }

    if (PathsEqualIgnoreCase(path, asset.relativePath)) {
        return true;
    }

    const std::string filename = Utf8Path(asset.relativePath).filename().string();
    if (!filename.empty() && PathsEqualIgnoreCase(path, filename)) {
        return true;
    }

    if (!toolscreenPath.empty()) {
        const std::filesystem::path rootPath(toolscreenPath);
        if (PathsEqualIgnoreCase(path, BuildAbsoluteBundledFontPath(rootPath, asset))) {
            return true;
        }

        if (std::string_view(asset.id) == "minecraft") {
            const std::string legacyRootPath = WideToUtf8((rootPath / "Minecraft.ttf").wstring());
            if (PathsEqualIgnoreCase(path, legacyRootPath)) {
                return true;
            }
        }
    }

    return false;
}

bool WriteEmbeddedResourceToFile(WORD resourceId, const std::filesystem::path& destination, const void* moduleAnchor,
                                 bool overwriteExisting) {
    if (moduleAnchor == nullptr) {
        return false;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(destination.parent_path(), directoryError);
    if (directoryError) {
        Log("WARNING: Failed to create bundled font directory: " + WideToUtf8(destination.parent_path().wstring()));
        return false;
    }

    HMODULE moduleHandle = nullptr;
    const BOOL gotModule = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(moduleAnchor), &moduleHandle);
    if (gotModule != TRUE || moduleHandle == nullptr) {
        Log("WARNING: Failed to resolve module handle while staging bundled font resource " + std::to_string(resourceId) + ".");
        return false;
    }

    HRSRC resourceHandle = FindResourceW(moduleHandle, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resourceHandle == nullptr) {
        Log("WARNING: Failed to find bundled font resource " + std::to_string(resourceId) + ".");
        return false;
    }

    HGLOBAL resourceDataHandle = LoadResource(moduleHandle, resourceHandle);
    if (resourceDataHandle == nullptr) {
        Log("WARNING: Failed to load bundled font resource " + std::to_string(resourceId) + ".");
        return false;
    }

    const DWORD resourceSize = SizeofResource(moduleHandle, resourceHandle);
    const void* resourceData = LockResource(resourceDataHandle);
    if (resourceData == nullptr || resourceSize == 0) {
        Log("WARNING: Bundled font resource was empty: " + std::to_string(resourceId) + ".");
        return false;
    }

    const DWORD creationDisposition = overwriteExisting ? CREATE_ALWAYS : CREATE_NEW;
    HANDLE fileHandle = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        const DWORD lastError = GetLastError();
        if (!overwriteExisting && lastError == ERROR_FILE_EXISTS) {
            return true;
        }

        Log("WARNING: Failed to open bundled font path for writing: " + WideToUtf8(destination.wstring()) +
            " (error " + std::to_string(lastError) + ").");
        return false;
    }

    DWORD written = 0;
    const BOOL wroteFile = WriteFile(fileHandle, resourceData, resourceSize, &written, nullptr);
    CloseHandle(fileHandle);
    if (wroteFile != TRUE || written != resourceSize) {
        Log("WARNING: Failed to stage bundled font resource to: " + WideToUtf8(destination.wstring()));
        return false;
    }

    return true;
}

} // namespace

const std::vector<BundledFontAsset>& GetBundledFontAssets() {
    static const std::vector<BundledFontAsset> assets = {
        { "open-sans", "font.preset.open_sans", "fonts/OpenSans-Regular.ttf", IDR_OPENSANS_FONT },
        { "minecraft", "font.preset.minecraft", "fonts/Minecraft.ttf", IDR_MINECRAFT_FONT },
        { "monocraft", "font.preset.monocraft", "fonts/Monocraft.ttf", IDR_MONOCRAFT_FONT },
    };

    return assets;
}

const BundledFontAsset* FindBundledFontAssetById(std::string_view id) {
    for (const BundledFontAsset& asset : GetBundledFontAssets()) {
        if (std::string_view(asset.id) == id) {
            return &asset;
        }
    }

    return nullptr;
}

const BundledFontAsset* FindBundledFontAssetByPath(const std::string& path, const std::wstring& toolscreenPath) {
    for (const BundledFontAsset& asset : GetBundledFontAssets()) {
        if (MatchesBundledFontPath(asset, path, toolscreenPath)) {
            return &asset;
        }
    }

    return nullptr;
}

std::string ResolveToolscreenRelativePath(const std::string& path, const std::wstring& toolscreenPath) {
    if (path.empty()) {
        return path;
    }

    const std::filesystem::path configuredPath = Utf8Path(path);
    if (configuredPath.is_absolute() || toolscreenPath.empty()) {
        return path;
    }

    return WideToUtf8((std::filesystem::path(toolscreenPath) / configuredPath).wstring());
}

std::string NormalizeBundledFontPath(const std::string& path, const std::wstring& toolscreenPath) {
    const BundledFontAsset* asset = FindBundledFontAssetByPath(path, toolscreenPath);
    if (asset == nullptr) {
        return path;
    }

    return asset->relativePath;
}

bool NormalizeBundledFontPathInPlace(std::string& path, const std::wstring& toolscreenPath) {
    const std::string normalizedPath = NormalizeBundledFontPath(path, toolscreenPath);
    if (normalizedPath == path) {
        return false;
    }

    path = normalizedPath;
    return true;
}

bool ExtractBundledFontAsset(const BundledFontAsset& asset, const std::filesystem::path& rootPath, const void* moduleAnchor,
                             bool overwriteExisting) {
    if (rootPath.empty()) {
        return false;
    }

    std::error_code existsError;
    const std::filesystem::path destination = rootPath / Utf8Path(asset.relativePath);
    if (!overwriteExisting && std::filesystem::exists(destination, existsError) && !existsError) {
        return true;
    }

    return WriteEmbeddedResourceToFile(asset.resourceId, destination, moduleAnchor, overwriteExisting);
}

void ExtractBundledFontAssets(const std::filesystem::path& rootPath, const void* moduleAnchor, bool overwriteExisting) {
    for (const BundledFontAsset& asset : GetBundledFontAssets()) {
        ExtractBundledFontAsset(asset, rootPath, moduleAnchor, overwriteExisting);
    }
}