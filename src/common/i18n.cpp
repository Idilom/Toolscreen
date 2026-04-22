#include "i18n.h"

#include "platform/resource.h"
#include "utils.h"

#include <atomic>
#include <unordered_map>

inline nlohmann::json                               g_langsJson;
inline nlohmann::json                               g_translationJson;
inline std::unordered_map<std::string, std::string> g_translationCache;
inline std::atomic<uint64_t>                        g_translationGeneration{ 0 };

void LoadLangs() {
    try {
        HMODULE hModule = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&RenderWelcomeToast, &hModule
        );
        if (!hModule) {
            throw std::exception("GetModuleHandleExW failed");
        }

        HRSRC resSrc = FindResourceW(hModule, MAKEINTRESOURCEW(IDR_LANG_LANGS), RT_RCDATA);
        if (!resSrc) {
            throw std::exception("FindResourceW failed");
        }

        HGLOBAL resHandle = LoadResource(hModule, resSrc);
        if (!resHandle) {
            throw std::exception("LoadResource failed");
        }

        auto* const resPtr = LockResource(resHandle);
        if (!resPtr) {
            throw std::exception("LockResource failed");
        }

        const auto resSize = SizeofResource(hModule, resSrc);
        if (resSize == 0) {
            throw std::exception("SizeofResource failed");
        }

        g_langsJson = nlohmann::json::parse((const char*)resPtr, (const char*)resPtr + resSize);
    } catch (const std::exception& e) {
        Log(std::string("Failed to load language list: ") + e.what());
    }
}

const nlohmann::json& GetLangs() {
    return g_langsJson;
}

bool LoadTranslation(const std::string& lang) {
    static const std::unordered_map<std::string, LPWSTR> langToResName = {
        {"en", MAKEINTRESOURCEW(IDR_LANG_EN)},
        {"zh_CN", MAKEINTRESOURCEW(IDR_LANG_ZH_CN)},
        {"pt_BR", MAKEINTRESOURCEW(IDR_LANG_PT_BR)},
    };

    try {
        nlohmann::json loadedTranslationJson;

        HMODULE hModule = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&RenderWelcomeToast, &hModule
        );
        if (!hModule) {
            throw std::exception("GetModuleHandleExW failed");
        }

        HRSRC resSrc = FindResourceW(hModule, langToResName.at(lang), RT_RCDATA);
        if (!resSrc) {
            throw std::exception("FindResourceW failed");
        }

        HGLOBAL resHandle = LoadResource(hModule, resSrc);
        if (!resHandle) {
            throw std::exception("LoadResource failed");
        }

        auto* const resPtr = LockResource(resHandle);
        if (!resPtr) {
            throw std::exception("LockResource failed");
        }

        const auto resSize = SizeofResource(hModule, resSrc);
        if (resSize == 0) {
            throw std::exception("SizeofResource failed");
        }

        loadedTranslationJson = nlohmann::json::parse((const char*)resPtr, (const char*)resPtr + resSize);

        g_translationJson = std::move(loadedTranslationJson);
        g_translationCache.clear();
        g_translationGeneration.fetch_add(1, std::memory_order_release);
    } catch (const std::exception& e) {
        Log("Failed to load translations of " + lang + ": " + e.what());
        return false;
    }
    return true;
}

uint64_t GetTranslationGeneration() {
    return g_translationGeneration.load(std::memory_order_acquire);
}

std::vector<ImWchar> BuildTranslationGlyphRanges() {
    static constexpr ImWchar kAsciiFallbackRange[] = { 0x0020, 0x00FF, 0 };

    ImFontGlyphRangesBuilder builder;
    if (ImGui::GetCurrentContext() != nullptr) {
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
    } else {
        builder.AddRanges(kAsciiFallbackRange);
    }

    auto addJsonStrings = [&builder](const nlohmann::json& json) {
        if (!json.is_object()) {
            return;
        }

        for (const auto& [key, value] : json.items()) {
            (void)key;
            if (!value.is_string()) {
                continue;
            }

            const auto& text = value.get_ref<const std::string&>();
            if (!text.empty()) {
                builder.AddText(text.c_str());
            }
        }
    };

    addJsonStrings(g_langsJson);
    addJsonStrings(g_translationJson);

    ImVector<ImWchar> imguiRanges;
    builder.BuildRanges(&imguiRanges);
    return std::vector<ImWchar>(imguiRanges.Data, imguiRanges.Data + imguiRanges.Size);
}

const std::string& tr_ref(const char* key) {
    auto cacheIt = g_translationCache.find(key);
    if (cacheIt != g_translationCache.end()) {
        return cacheIt->second;
    }

    if (!g_translationJson.contains(key)) {
        Log("Missing translation for key: " + std::string(key));
        return g_translationCache.emplace(key, key).first->second;
    }
    if (!g_translationJson[key].is_string()) {
        Log(std::format("Translation for key '{}' is not a string", key));
        return g_translationCache.emplace(key, key).first->second;
    }

    const std::string result = g_translationJson[key].get<std::string>();
    return g_translationCache.emplace(key, result).first->second;
}

std::string tr(const char* key) {
    return tr_ref(key);
}
