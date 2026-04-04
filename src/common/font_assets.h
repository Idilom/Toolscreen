#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

struct BundledFontAsset {
    const char* id = nullptr;
    const char* translationKey = nullptr;
    const char* relativePath = nullptr;
    WORD resourceId = 0;
};

const std::vector<BundledFontAsset>& GetBundledFontAssets();
const BundledFontAsset* FindBundledFontAssetById(std::string_view id);
const BundledFontAsset* FindBundledFontAssetByPath(const std::string& path, const std::wstring& toolscreenPath);

std::string ResolveToolscreenRelativePath(const std::string& path, const std::wstring& toolscreenPath);
std::string NormalizeBundledFontPath(const std::string& path, const std::wstring& toolscreenPath);
bool NormalizeBundledFontPathInPlace(std::string& path, const std::wstring& toolscreenPath);

bool ExtractBundledFontAsset(const BundledFontAsset& asset, const std::filesystem::path& rootPath, const void* moduleAnchor,
                             bool overwriteExisting = false);
void ExtractBundledFontAssets(const std::filesystem::path& rootPath, const void* moduleAnchor, bool overwriteExisting = false);