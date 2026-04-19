#pragma once

#include "features/ninjabrain_data.h"

#include <cstdint>
#include <string>
#include <vector>

struct NinjabrainInformationTextRun {
    std::string text;
    bool hasColor = false;
    uint32_t colorRgb = 0;
};

struct NinjabrainFormattedInformationMessage {
    std::string plainText;
    std::vector<NinjabrainInformationTextRun> runs;
};

NinjabrainFormattedInformationMessage FormatNinjabrainInformationMessage(
    const NinjabrainInformationMessage& message);