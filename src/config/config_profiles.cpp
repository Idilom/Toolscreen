#include "config_toml.h"
#include "common/utils.h"
#include "gui/gui.h"
#include "render/mirror_thread.h"
#include "render/render.h"
#include "runtime/logic_thread.h"
#include "common/mode_dimensions.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

ProfilesConfig g_profilesConfig;

static std::wstring GetProfilesDir() {
    return g_toolscreenPath + L"\\profiles";
}

static std::wstring GetProfilePath(const std::string& name) {
    return GetProfilesDir() + L"\\" + Utf8ToWide(name) + L".toml";
}

static std::wstring GetProfilesConfigPath() {
    return g_toolscreenPath + L"\\profiles.toml";
}

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

static bool ProfileNameExists(const std::string& name) {
    for (const auto& pm : g_profilesConfig.profiles) {
        if (pm.name == name) return true;
    }
    return false;
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
    profile.configVersion = full.configVersion;
}

static void ExtractProfileConfigMove(Config&& full, Config& profile) {
    profile = Config{};
    MoveProfileFields(full, profile);
    profile.configVersion = full.configVersion;
}

static void EnsureProfilesDirExists() {
    std::filesystem::create_directories(std::filesystem::path(GetProfilesDir()));
}

void SaveProfile(const std::string& name) {
    EnsureProfilesDirExists();
    Config profileConfig;
    ExtractProfileConfig(g_config, profileConfig);
    SaveConfigToTomlFile(profileConfig, GetProfilePath(name));
}

bool LoadProfile(const std::string& name) {
    std::wstring path = GetProfilePath(name);
    if (!std::filesystem::exists(std::filesystem::path(path))) {
        Log("LoadProfile: file not found for '" + name + "'");
        return false;
    }

    Config profileConfig;
    if (!LoadConfigFromTomlFile(path, profileConfig)) {
        Log("LoadProfile: failed to parse profile '" + name + "', using current config");
        return false;
    }

    ApplyProfileFields(profileConfig, g_config);
    return true;
}

bool LoadProfilesConfig() {
    std::wstring path = GetProfilesConfigPath();
    if (!std::filesystem::exists(std::filesystem::path(path))) return false;

    try {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file.is_open()) return false;

        auto tbl = toml::parse(file);
        g_profilesConfig.activeProfile = tbl["activeProfile"].value_or(std::string(kDefaultProfileName));
        g_profilesConfig.profiles.clear();

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
                    if (!pm.name.empty()) g_profilesConfig.profiles.push_back(pm);
                }
            }
        }
    } catch (...) {
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

void SaveProfilesConfig() {
    toml::table tbl;
    BuildProfilesConfigToml(tbl);

    std::ofstream file(std::filesystem::path(GetProfilesConfigPath()), std::ios::binary | std::ios::trunc);
    if (file.is_open()) {
        file << tbl;
    }
}

void SwitchProfile(const std::string& newProfileName) {
    Config oldProfileConfig;
    ExtractProfileConfig(g_config, oldProfileConfig);
    std::wstring oldProfilePath = GetProfilePath(g_profilesConfig.activeProfile);

    if (!LoadProfile(newProfileName)) return;

    g_profilesConfig.activeProfile = newProfileName;

    toml::table profilesMetaTbl;
    BuildProfilesConfigToml(profilesMetaTbl);
    std::wstring profilesMetaPath = GetProfilesConfigPath();

    std::thread([oldProfileConfig = std::move(oldProfileConfig), oldProfilePath,
                 profilesMetaTbl = std::move(profilesMetaTbl), profilesMetaPath]() {
        try { SaveConfigToTomlFile(oldProfileConfig, oldProfilePath); }
        catch (...) { Log("SwitchProfile: failed to save old profile"); }
        try {
            std::ofstream f(std::filesystem::path(profilesMetaPath), std::ios::binary | std::ios::trunc);
            if (f.is_open()) { f << profilesMetaTbl; }
        } catch (...) { Log("SwitchProfile: failed to save profiles metadata"); }
    }).detach();

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
    if (!IsValidProfileName(name) || ProfileNameExists(name)) return false;

    Config defaultConfig;
    LoadEmbeddedDefaultConfig(defaultConfig);

    Config profileConfig;
    ExtractProfileConfigMove(std::move(defaultConfig), profileConfig);
    SaveConfigToTomlFile(profileConfig, GetProfilePath(name));

    ProfileMetadata pm;
    pm.name = name;
    g_profilesConfig.profiles.push_back(pm);
    SaveProfilesConfig();
    return true;
}

bool DuplicateProfile(const std::string& srcName, const std::string& dstName) {
    if (!IsValidProfileName(dstName) || ProfileNameExists(dstName)) return false;

    if (srcName == g_profilesConfig.activeProfile) {
        SaveProfile(srcName);
    }

    std::wstring srcPath = GetProfilePath(srcName);
    std::wstring dstPath = GetProfilePath(dstName);

    try {
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
    } catch (...) {
        return false;
    }

    ProfileMetadata pm;
    pm.name = dstName;
    for (const auto& existing : g_profilesConfig.profiles) {
        if (existing.name == srcName) {
            pm.color[0] = existing.color[0];
            pm.color[1] = existing.color[1];
            pm.color[2] = existing.color[2];
            break;
        }
    }
    g_profilesConfig.profiles.push_back(pm);
    SaveProfilesConfig();
    return true;
}

void DeleteProfile(const std::string& name) {
    if (g_profilesConfig.profiles.size() <= 1) return;
    if (name == g_profilesConfig.activeProfile) return;

    try {
        std::filesystem::remove(std::filesystem::path(GetProfilePath(name)));
    } catch (const std::exception& e) {
        Log("DeleteProfile: failed to delete file for '" + name + "': " + e.what());
    } catch (...) {
        Log("DeleteProfile: failed to delete file for '" + name + "'");
    }

    auto& profiles = g_profilesConfig.profiles;
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                  [&](const ProfileMetadata& p) { return p.name == name; }),
                   profiles.end());
    SaveProfilesConfig();
}

bool RenameProfile(const std::string& oldName, const std::string& newName) {
    if (oldName == newName) return true;
    if (!IsValidProfileName(newName) || ProfileNameExists(newName)) return false;

    for (auto& pm : g_profilesConfig.profiles) {
        if (pm.name == oldName) {
            pm.name = newName;
            break;
        }
    }
    bool wasActive = (g_profilesConfig.activeProfile == oldName);
    if (wasActive) g_profilesConfig.activeProfile = newName;
    SaveProfilesConfig();

    try {
        std::filesystem::rename(std::filesystem::path(GetProfilePath(oldName)),
                                std::filesystem::path(GetProfilePath(newName)));
    } catch (...) {
        for (auto& pm : g_profilesConfig.profiles) {
            if (pm.name == newName) { pm.name = oldName; break; }
        }
        if (wasActive) g_profilesConfig.activeProfile = oldName;
        SaveProfilesConfig();
        return false;
    }
    return true;
}

bool MigrateToProfiles() {
    if (std::filesystem::exists(std::filesystem::path(GetProfilesDir()))) return false;

    EnsureProfilesDirExists();
    if (!std::filesystem::exists(std::filesystem::path(GetProfilesDir()))) {
        Log("MigrateToProfiles: failed to create profiles directory");
        return false;
    }

    SaveProfile(kDefaultProfileName);

    g_profilesConfig.activeProfile = kDefaultProfileName;
    g_profilesConfig.profiles.clear();
    ProfileMetadata pm;
    pm.name = kDefaultProfileName;
    g_profilesConfig.profiles.push_back(pm);
    SaveProfilesConfig();

    return true;
}
