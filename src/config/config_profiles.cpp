#include "config_toml.h"
#include "common/utils.h"
#include "gui/gui.h"
#include "render/mirror_thread.h"
#include "render/render.h"
#include "runtime/logic_thread.h"
#include "common/mode_dimensions.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

ProfilesConfig g_profilesConfig;

namespace {

std::mutex g_profilesMutex;

static std::wstring GetProfilesDir() {
    return g_toolscreenPath + L"\\profiles";
}

static std::wstring GetProfilePath(const std::string& name) {
    return GetProfilesDir() + L"\\" + Utf8ToWide(name) + L".toml";
}

static std::wstring GetProfilesConfigPath() {
    return g_toolscreenPath + L"\\profiles.toml";
}

static bool ProfileNamesEqual(const std::string& a, const std::string& b) {
    return EqualsIgnoreCase(a, b);
}

static void CopyColor(float dst[3], const float src[3]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static bool ColorsEqual(const float a[3], const float b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static std::wstring MakeTempSiblingPath(const std::wstring& finalPath, const wchar_t* suffix) {
    const std::filesystem::path final(finalPath);
    const std::wstring filename = final.filename().wstring();
    return (final.parent_path() /
            (filename + suffix + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())))
        .wstring();
}

static bool ReplacePathAtomically(const std::wstring& tempPath, const std::wstring& finalPath) {
    if (MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    std::error_code removeError;
    std::filesystem::remove(std::filesystem::path(finalPath), removeError);
    if (MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    std::error_code cleanupError;
    std::filesystem::remove(std::filesystem::path(tempPath), cleanupError);
    return false;
}

static bool RenamePathReplacingExisting(const std::wstring& fromPath, const std::wstring& toPath) {
    if (fromPath == toPath) {
        return true;
    }

    if (MoveFileExW(fromPath.c_str(), toPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    if (_wcsicmp(fromPath.c_str(), toPath.c_str()) != 0) {
        return false;
    }

    const std::wstring tempPath = MakeTempSiblingPath(fromPath, L".rename-");
    if (!MoveFileExW(fromPath.c_str(), tempPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }

    if (MoveFileExW(tempPath.c_str(), toPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }

    MoveFileExW(tempPath.c_str(), fromPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return false;
}

static void EnsureProfilesDirExists() {
    std::filesystem::create_directories(std::filesystem::path(GetProfilesDir()));
}

static bool SaveConfigAtomically(const Config& config, const std::wstring& path) {
    std::error_code dirError;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), dirError);

    const std::wstring tempPath = MakeTempSiblingPath(path, L".tmp-");
    if (!SaveConfigToTomlFile(config, tempPath)) {
        std::error_code cleanupError;
        std::filesystem::remove(std::filesystem::path(tempPath), cleanupError);
        return false;
    }

    if (!ReplacePathAtomically(tempPath, path)) {
        std::error_code cleanupError;
        std::filesystem::remove(std::filesystem::path(tempPath), cleanupError);
        return false;
    }

    return true;
}

static bool WriteTomlTableAtomically(const toml::table& tbl, const std::wstring& path) {
    std::error_code dirError;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), dirError);

    const std::wstring tempPath = MakeTempSiblingPath(path, L".tmp-");
    {
        std::ofstream out(std::filesystem::path(tempPath), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << tbl;
        out.close();
    }

    if (!ReplacePathAtomically(tempPath, path)) {
        std::error_code cleanupError;
        std::filesystem::remove(std::filesystem::path(tempPath), cleanupError);
        return false;
    }

    return true;
}

static ProfileMetadata* FindProfileMetadataLocked(const std::string& name) {
    for (auto& pm : g_profilesConfig.profiles) {
        if (ProfileNamesEqual(pm.name, name)) {
            return &pm;
        }
    }
    return nullptr;
}

static const ProfileMetadata* FindProfileMetadataLockedConst(const std::string& name) {
    for (const auto& pm : g_profilesConfig.profiles) {
        if (ProfileNamesEqual(pm.name, name)) {
            return &pm;
        }
    }
    return nullptr;
}

static std::string ResolveTrackedProfileNameLocked(const std::string& name) {
    const ProfileMetadata* pm = FindProfileMetadataLockedConst(name);
    return pm ? pm->name : std::string();
}

static bool ProfileNameExistsLocked(const std::string& name, const std::string& excludeName = std::string()) {
    for (const auto& pm : g_profilesConfig.profiles) {
        if (!excludeName.empty() && ProfileNamesEqual(pm.name, excludeName)) {
            continue;
        }
        if (ProfileNamesEqual(pm.name, name)) {
            return true;
        }
    }
    return false;
}

static bool LoadProfileConfigFromPath(const std::wstring& path, Config& profileConfig) {
    if (!std::filesystem::exists(std::filesystem::path(path))) {
        return false;
    }
    return LoadConfigFromTomlFile(path, profileConfig);
}

static std::vector<std::string> ListProfilesOnDiskLocked() {
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path profilesDir = GetProfilesDir();
    if (!std::filesystem::exists(profilesDir, error)) {
        return names;
    }

    for (const auto& entry : std::filesystem::directory_iterator(profilesDir, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || error) {
            continue;
        }
        if (entry.path().extension() != L".toml") {
            continue;
        }
        names.push_back(WideToUtf8(entry.path().stem().wstring()));
    }

    std::sort(names.begin(), names.end());
    return names;
}

static bool LoadProfilesConfigLocked() {
    g_profilesConfig = ProfilesConfig();

    const std::wstring path = GetProfilesConfigPath();
    if (!std::filesystem::exists(std::filesystem::path(path))) {
        return false;
    }

    try {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        auto tbl = toml::parse(file);
        g_profilesConfig.activeProfile = tbl["activeProfile"].value_or(std::string(kDefaultProfileName));

        if (auto arr = tbl["profile"].as_array()) {
            for (const auto& elem : *arr) {
                if (auto t = elem.as_table()) {
                    ProfileMetadata pm;
                    pm.name = (*t)["name"].value_or(std::string(""));
                    if (auto colorArr = (*t)["color"].as_array(); colorArr && colorArr->size() >= 3) {
                        pm.color[0] = (*colorArr)[0].value_or(kDefaultProfileColor[0]);
                        pm.color[1] = (*colorArr)[1].value_or(kDefaultProfileColor[1]);
                        pm.color[2] = (*colorArr)[2].value_or(kDefaultProfileColor[2]);
                    }
                    if (!pm.name.empty() && !ProfileNameExistsLocked(pm.name)) {
                        g_profilesConfig.profiles.push_back(pm);
                    }
                }
            }
        }
    } catch (...) {
        g_profilesConfig = ProfilesConfig();
        return false;
    }

    return true;
}

static void BuildProfilesConfigToml(toml::table& tbl) {
    tbl.insert("activeProfile", g_profilesConfig.activeProfile);

    toml::array profilesArr;
    for (const auto& pm : g_profilesConfig.profiles) {
        toml::table pt;
        pt.insert("name", pm.name);
        toml::array colorArr;
        colorArr.push_back(pm.color[0]);
        colorArr.push_back(pm.color[1]);
        colorArr.push_back(pm.color[2]);
        pt.insert("color", colorArr);
        profilesArr.push_back(pt);
    }
    tbl.insert("profile", profilesArr);
}

static bool SaveProfilesConfigLocked() {
    toml::table tbl;
    BuildProfilesConfigToml(tbl);
    return WriteTomlTableAtomically(tbl, GetProfilesConfigPath());
}

static bool ProfilesConfigMatchesLocked(const std::vector<ProfileMetadata>& profiles, const std::string& activeProfile) {
    if (!ProfileNamesEqual(g_profilesConfig.activeProfile, activeProfile)) {
        return false;
    }
    if (g_profilesConfig.profiles.size() != profiles.size()) {
        return false;
    }

    for (size_t i = 0; i < profiles.size(); ++i) {
        if (g_profilesConfig.profiles[i].name != profiles[i].name) {
            return false;
        }
        if (!ColorsEqual(g_profilesConfig.profiles[i].color, profiles[i].color)) {
            return false;
        }
    }

    return true;
}

static bool SyncProfilesConfigWithDiskLocked(bool seedFromCurrentConfigIfEmpty) {
    EnsureProfilesDirExists();

    std::vector<std::string> profileNames = ListProfilesOnDiskLocked();
    if (profileNames.empty() && seedFromCurrentConfigIfEmpty) {
        Config defaultProfile;
        defaultProfile = Config{};
        ApplyProfileFields(g_config, defaultProfile);
        defaultProfile.configVersion = g_config.configVersion;
        if (!SaveConfigAtomically(defaultProfile, GetProfilePath(kDefaultProfileName))) {
            Log("EnsureProfilesConfigReady: failed to seed default profile file");
            return false;
        }
        profileNames.push_back(kDefaultProfileName);
    }

    if (profileNames.empty()) {
        g_profilesConfig = ProfilesConfig();
        return false;
    }

    std::vector<ProfileMetadata> normalizedProfiles;
    normalizedProfiles.reserve(profileNames.size());
    for (const auto& profileName : profileNames) {
        ProfileMetadata normalized;
        normalized.name = profileName;
        if (const ProfileMetadata* existing = FindProfileMetadataLockedConst(profileName)) {
            CopyColor(normalized.color, existing->color);
        }
        normalizedProfiles.push_back(normalized);
    }

    std::string resolvedActiveProfile = ResolveTrackedProfileNameLocked(g_profilesConfig.activeProfile);
    if (resolvedActiveProfile.empty()) {
        for (const auto& pm : normalizedProfiles) {
            if (ProfileNamesEqual(pm.name, kDefaultProfileName)) {
                resolvedActiveProfile = pm.name;
                break;
            }
        }
    }
    if (resolvedActiveProfile.empty()) {
        resolvedActiveProfile = normalizedProfiles.front().name;
    }

    if (!ProfilesConfigMatchesLocked(normalizedProfiles, resolvedActiveProfile)) {
        g_profilesConfig.profiles = std::move(normalizedProfiles);
        g_profilesConfig.activeProfile = resolvedActiveProfile;
        if (!SaveProfilesConfigLocked()) {
            Log("EnsureProfilesConfigReady: failed to persist rebuilt profiles metadata");
        }
    }

    return true;
}

static bool SaveProfileSnapshotLocked(const std::string& name, const Config& configSnapshot) {
    EnsureProfilesDirExists();
    Config normalizedSnapshot = configSnapshot;
    normalizedSnapshot.configVersion = GetConfigVersion();
    return SaveConfigAtomically(normalizedSnapshot, GetProfilePath(name));
}

} // namespace

bool IsValidProfileName(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') return false;
    static const std::vector<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
        "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"
    };
    std::string upper = name;
    for (auto& c : upper) c = (char)toupper((unsigned char)c);
    for (const auto& r : reserved) {
        if (upper == r) return false;
    }
    if (name.find("..") != std::string::npos) return false;
    return true;
}

void ApplyProfileFields(const Config& src, Config& dst) {
    dst.mirrors = src.mirrors;
    dst.mirrorGroups = src.mirrorGroups;
    dst.images = src.images;
    dst.windowOverlays = src.windowOverlays;
    dst.browserOverlays = src.browserOverlays;
    dst.modes = src.modes;
    dst.hotkeys = src.hotkeys;
    dst.sensitivityHotkeys = src.sensitivityHotkeys;
    dst.eyezoom = src.eyezoom;
    dst.defaultMode = src.defaultMode;
    dst.keyRebinds = src.keyRebinds;
    dst.cursors = src.cursors;
    dst.mouseSensitivity = src.mouseSensitivity;
    dst.windowsMouseSpeed = src.windowsMouseSpeed;
    dst.borderlessHotkey = src.borderlessHotkey;
    dst.autoBorderless = src.autoBorderless;
    dst.imageOverlaysHotkey = src.imageOverlaysHotkey;
    dst.windowOverlaysHotkey = src.windowOverlaysHotkey;
    dst.hideAnimationsInGame = src.hideAnimationsInGame;
}

static void MoveProfileFields(Config& src, Config& dst) {
    dst.mirrors = std::move(src.mirrors);
    dst.mirrorGroups = std::move(src.mirrorGroups);
    dst.images = std::move(src.images);
    dst.windowOverlays = std::move(src.windowOverlays);
    dst.browserOverlays = std::move(src.browserOverlays);
    dst.modes = std::move(src.modes);
    dst.hotkeys = std::move(src.hotkeys);
    dst.sensitivityHotkeys = std::move(src.sensitivityHotkeys);
    dst.eyezoom = std::move(src.eyezoom);
    dst.defaultMode = std::move(src.defaultMode);
    dst.keyRebinds = std::move(src.keyRebinds);
    dst.cursors = std::move(src.cursors);
    dst.mouseSensitivity = src.mouseSensitivity;
    dst.windowsMouseSpeed = src.windowsMouseSpeed;
    dst.borderlessHotkey = std::move(src.borderlessHotkey);
    dst.autoBorderless = src.autoBorderless;
    dst.imageOverlaysHotkey = std::move(src.imageOverlaysHotkey);
    dst.windowOverlaysHotkey = std::move(src.windowOverlaysHotkey);
    dst.hideAnimationsInGame = src.hideAnimationsInGame;
}

static void ExtractProfileConfig(const Config& full, Config& profile) {
    profile = Config{};
    ApplyProfileFields(full, profile);
    profile.configVersion = GetConfigVersion();
}

static void ExtractProfileConfigMove(Config&& full, Config& profile) {
    profile = Config{};
    MoveProfileFields(full, profile);
    profile.configVersion = GetConfigVersion();
}

void SaveProfile(const std::string& name) {
    Config profileConfig;
    ExtractProfileConfig(g_config, profileConfig);
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    if (!SaveProfileSnapshotLocked(name, profileConfig)) {
        Log("SaveProfile: failed to write profile '" + name + "'");
    }
}

bool SaveProfileSnapshot(const std::string& name, const Config& configSnapshot) {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    return SaveProfileSnapshotLocked(name, configSnapshot);
}

bool SaveProfileSnapshotIfTracked(const std::string& name, const Config& configSnapshot) {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    const std::string trackedName = ResolveTrackedProfileNameLocked(name);
    if (trackedName.empty()) {
        return false;
    }
    return SaveProfileSnapshotLocked(trackedName, configSnapshot);
}

bool LoadProfile(const std::string& name) {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(g_profilesMutex);
        const std::string trackedName = ResolveTrackedProfileNameLocked(name);
        path = GetProfilePath(trackedName.empty() ? name : trackedName);
    }

    Config profileConfig;
    if (!LoadProfileConfigFromPath(path, profileConfig)) {
        Log("LoadProfile: failed to parse profile '" + name + "', using current config");
        return false;
    }

    ApplyProfileFields(profileConfig, g_config);
    return true;
}

bool LoadProfilesConfig() {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    return LoadProfilesConfigLocked();
}

void SaveProfilesConfig() {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    if (!SaveProfilesConfigLocked()) {
        Log("SaveProfilesConfig: failed to write profiles metadata");
    }
}

bool EnsureProfilesConfigReady() {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    LoadProfilesConfigLocked();
    return SyncProfilesConfigWithDiskLocked(true);
}

void SwitchProfile(const std::string& newProfileName) {
    Config oldProfileConfig;
    Config newProfileConfig;
    std::string resolvedNewProfileName;
    bool failedToSavePreviousProfile = false;
    bool failedToSaveProfilesMetadata = false;

    {
        std::lock_guard<std::mutex> lock(g_profilesMutex);
        resolvedNewProfileName = ResolveTrackedProfileNameLocked(newProfileName);
        if (resolvedNewProfileName.empty()) {
            resolvedNewProfileName = newProfileName;
        }

        if (!LoadProfileConfigFromPath(GetProfilePath(resolvedNewProfileName), newProfileConfig)) {
            Log("LoadProfile: failed to parse profile '" + resolvedNewProfileName + "', using current config");
            return;
        }

        ExtractProfileConfig(g_config, oldProfileConfig);
        const std::string previousTrackedProfileName = ResolveTrackedProfileNameLocked(g_profilesConfig.activeProfile);
        const std::string previousProfileName = previousTrackedProfileName.empty() ? g_profilesConfig.activeProfile : previousTrackedProfileName;

        if (!previousProfileName.empty() && !SaveProfileSnapshotLocked(previousProfileName, oldProfileConfig)) {
            failedToSavePreviousProfile = true;
        }

        g_profilesConfig.activeProfile = resolvedNewProfileName;
        if (!SaveProfilesConfigLocked()) {
            failedToSaveProfilesMetadata = true;
        }
    }

    if (failedToSavePreviousProfile) {
        Log("SwitchProfile: failed to save previous profile before switching");
    }
    if (failedToSaveProfilesMetadata) {
        Log("SwitchProfile: failed to save profiles metadata");
    }

    ApplyProfileFields(newProfileConfig, g_config);

    RemoveInvalidHotkeyModeReferences(g_config);
    ResetAllHotkeySecondaryModes(g_config);
    {
        std::lock_guard<std::mutex> lock(g_modeIdMutex);
        g_currentModeId = g_config.defaultMode;
        int nextIndex = 1 - g_currentModeIdIndex.load(std::memory_order_relaxed);
        g_modeIdBuffers[nextIndex] = g_config.defaultMode;
        g_currentModeIdIndex.store(nextIndex, std::memory_order_release);
    }
    WriteCurrentModeToFile(g_config.defaultMode);

    {
        std::lock_guard<std::mutex> lock(g_hotkeyMainKeysMutex);
        RebuildHotkeyMainKeys_Internal();
    }

    InvalidateConfigLookupCaches();

    {
        std::lock_guard<std::mutex> imgLock(g_userImagesMutex);
        std::lock_guard<std::mutex> delLock(g_texturesToDeleteMutex);
        for (const auto& [id, inst] : g_userImages) {
            if (inst.isAnimated) {
                for (GLuint tex : inst.frameTextures) {
                    if (tex != 0) g_texturesToDelete.push_back(tex);
                }
            } else if (inst.textureId != 0) {
                g_texturesToDelete.push_back(inst.textureId);
            }
        }
        g_userImages.clear();
        g_hasTexturesToDelete.store(true, std::memory_order_release);
    }

    g_allImagesLoaded = false;
    g_pendingImageLoad = true;
    RecalculateModeDimensions();
    RequestScreenMetricsRecalculation();
    PublishConfigSnapshot();

    g_configIsDirty = false;
}

bool CreateNewProfile(const std::string& name) {
    if (!IsValidProfileName(name)) return false;

    Config defaultConfig;
    LoadEmbeddedDefaultConfig(defaultConfig);

    Config profileConfig;
    ExtractProfileConfigMove(std::move(defaultConfig), profileConfig);

    std::lock_guard<std::mutex> lock(g_profilesMutex);
    if (ProfileNameExistsLocked(name)) return false;
    if (!SaveProfileSnapshotLocked(name, profileConfig)) return false;

    ProfileMetadata pm;
    pm.name = name;
    g_profilesConfig.profiles.push_back(pm);
    if (!SaveProfilesConfigLocked()) {
        g_profilesConfig.profiles.pop_back();
        std::error_code cleanupError;
        std::filesystem::remove(std::filesystem::path(GetProfilePath(name)), cleanupError);
        return false;
    }

    return true;
}

bool DuplicateProfile(const std::string& srcName, const std::string& dstName) {
    if (!IsValidProfileName(dstName)) return false;

    std::lock_guard<std::mutex> lock(g_profilesMutex);
    if (ProfileNameExistsLocked(dstName)) return false;

    const std::string resolvedSourceName = ResolveTrackedProfileNameLocked(srcName);
    const std::string trackedSourceName = resolvedSourceName.empty() ? srcName : resolvedSourceName;
    if (ProfileNamesEqual(trackedSourceName, g_profilesConfig.activeProfile)) {
        Config activeSnapshot;
        ExtractProfileConfig(g_config, activeSnapshot);
        if (!SaveProfileSnapshotLocked(trackedSourceName, activeSnapshot)) {
            return false;
        }
    }

    const std::wstring srcPath = GetProfilePath(trackedSourceName);
    const std::wstring dstPath = GetProfilePath(dstName);
    try {
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        return false;
    }

    ProfileMetadata pm;
    pm.name = dstName;
    if (const ProfileMetadata* existing = FindProfileMetadataLockedConst(trackedSourceName)) {
        CopyColor(pm.color, existing->color);
    }
    g_profilesConfig.profiles.push_back(pm);
    if (!SaveProfilesConfigLocked()) {
        g_profilesConfig.profiles.pop_back();
        std::error_code cleanupError;
        std::filesystem::remove(std::filesystem::path(dstPath), cleanupError);
        return false;
    }

    return true;
}

void DeleteProfile(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    const std::string trackedName = ResolveTrackedProfileNameLocked(name);
    if (trackedName.empty()) return;
    if (g_profilesConfig.profiles.size() <= 1) return;
    if (ProfileNamesEqual(trackedName, g_profilesConfig.activeProfile)) return;

    const std::vector<ProfileMetadata> previousProfiles = g_profilesConfig.profiles;
    auto& profiles = g_profilesConfig.profiles;
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                  [&](const ProfileMetadata& p) { return ProfileNamesEqual(p.name, trackedName); }),
                   profiles.end());

    if (!SaveProfilesConfigLocked()) {
        g_profilesConfig.profiles = previousProfiles;
        Log("DeleteProfile: failed to update profiles metadata for '" + trackedName + "'");
        return;
    }

    try {
        std::filesystem::remove(std::filesystem::path(GetProfilePath(trackedName)));
    } catch (const std::exception& e) {
        g_profilesConfig.profiles = previousProfiles;
        SaveProfilesConfigLocked();
        Log("DeleteProfile: failed to delete file for '" + trackedName + "': " + e.what());
    } catch (...) {
        g_profilesConfig.profiles = previousProfiles;
        SaveProfilesConfigLocked();
        Log("DeleteProfile: failed to delete file for '" + trackedName + "'");
    }
}

bool UpdateProfileMetadata(const std::string& currentName, const std::string& newName, const float color[3]) {
    if (!IsValidProfileName(newName)) return false;

    std::lock_guard<std::mutex> lock(g_profilesMutex);
    ProfileMetadata* existing = FindProfileMetadataLocked(currentName);
    if (!existing) return false;
    if (ProfileNameExistsLocked(newName, existing->name)) return false;

    const ProfileMetadata previousMetadata = *existing;
    const std::string previousActiveProfile = g_profilesConfig.activeProfile;
    const bool renameRequested = previousMetadata.name != newName;

    if (renameRequested) {
        const std::wstring oldPath = GetProfilePath(previousMetadata.name);
        const std::wstring newPath = GetProfilePath(newName);
        if (!RenamePathReplacingExisting(oldPath, newPath)) {
            return false;
        }
    }

    existing->name = newName;
    CopyColor(existing->color, color);
    if (ProfileNamesEqual(g_profilesConfig.activeProfile, previousMetadata.name)) {
        g_profilesConfig.activeProfile = newName;
    }

    if (!SaveProfilesConfigLocked()) {
        if (renameRequested) {
            RenamePathReplacingExisting(GetProfilePath(newName), GetProfilePath(previousMetadata.name));
        }
        *existing = previousMetadata;
        g_profilesConfig.activeProfile = previousActiveProfile;
        return false;
    }

    return true;
}

bool RenameProfile(const std::string& oldName, const std::string& newName) {
    float color[3] = { kDefaultProfileColor[0], kDefaultProfileColor[1], kDefaultProfileColor[2] };
    {
        std::lock_guard<std::mutex> lock(g_profilesMutex);
        const ProfileMetadata* existing = FindProfileMetadataLockedConst(oldName);
        if (!existing) return false;
        CopyColor(color, existing->color);
    }
    return UpdateProfileMetadata(oldName, newName, color);
}

bool MigrateToProfiles() {
    std::lock_guard<std::mutex> lock(g_profilesMutex);
    if (std::filesystem::exists(std::filesystem::path(GetProfilesDir()))) return false;

    EnsureProfilesDirExists();
    if (!std::filesystem::exists(std::filesystem::path(GetProfilesDir()))) {
        Log("MigrateToProfiles: failed to create profiles directory");
        return false;
    }

    Config defaultProfile;
    ExtractProfileConfig(g_config, defaultProfile);
    if (!SaveProfileSnapshotLocked(kDefaultProfileName, defaultProfile)) {
        Log("MigrateToProfiles: failed to save default profile");
        return false;
    }

    g_profilesConfig.activeProfile = kDefaultProfileName;
    g_profilesConfig.profiles.clear();
    ProfileMetadata pm;
    pm.name = kDefaultProfileName;
    g_profilesConfig.profiles.push_back(pm);
    if (!SaveProfilesConfigLocked()) {
        Log("MigrateToProfiles: failed to save profiles metadata");
        return false;
    }

    return true;
}
