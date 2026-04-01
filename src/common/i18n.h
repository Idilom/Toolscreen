#pragma once

#include <array>
#include <algorithm>
#include <format>
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <vector>

void LoadLangs();

const nlohmann::json& GetLangs();

bool LoadTranslation(const std::string& lang);

uint64_t GetTranslationGeneration();

std::vector<ImWchar> BuildTranslationGlyphRanges();

const std::string& tr_ref(const char* key);

std::string tr(const char* key);

template <typename... Args>
inline std::string tr(const char* key, const Args&... args) {
    return std::vformat(tr(key), std::make_format_args(args...));
}

inline const char* trc(const char* key) {
    return tr_ref(key).c_str();
}

template <typename Arg0, typename... Args>
inline const char* trc(const char* key, const Arg0& arg0, const Args&... args) {
    thread_local std::array<std::string, 32> formattedBuffers;
    thread_local size_t nextBufferIndex = 0;

    std::string& buffer = formattedBuffers[nextBufferIndex++ % formattedBuffers.size()];
    buffer = std::vformat(tr_ref(key), std::make_format_args(arg0, args...));
    return buffer.c_str();
}
