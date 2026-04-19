#include "ninjabrain_information_messages.h"

#include "common/i18n.h"

#include <cctype>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

constexpr char kCombinedCertaintyDefaultColor[] = "#00CE29";

bool IsHexDigit(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

bool IsAsciiDigit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

std::string NormalizeHexColor(std::string color) {
    if (color.empty()) {
        return kCombinedCertaintyDefaultColor;
    }

    if (color.front() != '#') {
        color.insert(color.begin(), '#');
    }

    if (color.size() != 7) {
        return kCombinedCertaintyDefaultColor;
    }

    for (size_t index = 1; index < color.size(); ++index) {
        if (!IsHexDigit(color[index])) {
            return kCombinedCertaintyDefaultColor;
        }
        color[index] = static_cast<char>(std::toupper(static_cast<unsigned char>(color[index])));
    }

    return color;
}

bool TryParseHexColor(std::string_view text, uint32_t& outColorRgb) {
    if (text.size() != 7 || text.front() != '#') {
        return false;
    }

    uint32_t value = 0;
    for (size_t index = 1; index < text.size(); ++index) {
        const char ch = text[index];
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            value |= static_cast<uint32_t>(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<uint32_t>(10 + ch - 'A');
        } else {
            return false;
        }
    }

    outColorRgb = value;
    return true;
}

std::string BuildColorSpanMarkup(const std::string& text, const std::string& color) {
    return "<span style=\"color:" + NormalizeHexColor(color) + ";\">" + text + "</span>";
}

bool TryExtractFirstColorSpan(const std::string& text, std::string& outColor, std::string& outInnerText) {
    const size_t spanStart = text.find("<span");
    if (spanStart == std::string::npos) {
        return false;
    }

    const size_t tagEnd = text.find('>', spanStart);
    if (tagEnd == std::string::npos) {
        return false;
    }

    const size_t closeTagStart = text.find("</span>", tagEnd + 1);
    if (closeTagStart == std::string::npos) {
        return false;
    }

    const std::string_view openTag(text.data() + spanStart, tagEnd - spanStart + 1);
    const size_t hashIndex = openTag.find('#');
    if (hashIndex == std::string_view::npos || hashIndex + 7 > openTag.size()) {
        return false;
    }

    const std::string_view colorCandidate = openTag.substr(hashIndex, 7);
    uint32_t parsedColor = 0;
    if (!TryParseHexColor(colorCandidate, parsedColor)) {
        return false;
    }

    (void)parsedColor;
    outColor = NormalizeHexColor(std::string(colorCandidate));
    outInnerText = text.substr(tagEnd + 1, closeTagStart - (tagEnd + 1));
    return true;
}

bool TryExtractLeadingIntegers(const std::string& text, size_t requiredCount, std::vector<int>& outValues) {
    outValues.clear();

    const char* cursor = text.c_str();
    while (*cursor != '\0' && outValues.size() < requiredCount) {
        if (((*cursor == '+' || *cursor == '-') && IsAsciiDigit(cursor[1])) || IsAsciiDigit(*cursor)) {
            char* end = nullptr;
            const long value = std::strtol(cursor, &end, 10);
            if (end != cursor) {
                outValues.push_back(static_cast<int>(value));
                cursor = end;
                continue;
            }
        }

        ++cursor;
    }

    return outValues.size() >= requiredCount;
}

std::string TranslateMarkupForInformationMessage(const NinjabrainInformationMessage& message) {
    if (message.type == "MISMEASURE") {
        return tr("ninjabrain.info_message.mismeasure");
    }

    if (message.type == "PORTAL_LINKING") {
        return tr("ninjabrain.info_message.portal_linking");
    }

    if (message.type == "MC_VERSION") {
        return tr("ninjabrain.info_message.mc_version");
    }

    if (message.type == "NEXT_THROW_DIRECTION") {
        std::vector<int> values;
        if (TryExtractLeadingIntegers(message.message, 2, values)) {
            return tr("ninjabrain.info_message.next_throw_direction", values[0], values[1]);
        }
        return message.message;
    }

    if (message.type == "COMBINED_CERTAINTY") {
        std::vector<int> values;
        std::string color;
        std::string certaintyText;
        if (TryExtractLeadingIntegers(message.message, 2, values) &&
            TryExtractFirstColorSpan(message.message, color, certaintyText)) {
            return tr(
                "ninjabrain.info_message.combined_certainty",
                values[0],
                values[1],
                BuildColorSpanMarkup(certaintyText, color));
        }
        return message.message;
    }

    return message.message;
}

void AppendRun(
    NinjabrainFormattedInformationMessage& formatted,
    std::string text,
    bool hasColor,
    uint32_t colorRgb) {
    if (text.empty()) {
        return;
    }

    formatted.plainText += text;

    if (!formatted.runs.empty()) {
        auto& previousRun = formatted.runs.back();
        if (previousRun.hasColor == hasColor && (!hasColor || previousRun.colorRgb == colorRgb)) {
            previousRun.text += text;
            return;
        }
    }

    formatted.runs.push_back({std::move(text), hasColor, colorRgb});
}

NinjabrainFormattedInformationMessage ParseMarkupToRuns(const std::string& markup) {
    NinjabrainFormattedInformationMessage formatted;

    size_t cursor = 0;
    while (cursor < markup.size()) {
        const size_t spanStart = markup.find("<span", cursor);
        if (spanStart == std::string::npos) {
            AppendRun(formatted, markup.substr(cursor), false, 0);
            break;
        }

        if (spanStart > cursor) {
            AppendRun(formatted, markup.substr(cursor, spanStart - cursor), false, 0);
        }

        const size_t tagEnd = markup.find('>', spanStart);
        if (tagEnd == std::string::npos) {
            AppendRun(formatted, markup.substr(spanStart), false, 0);
            break;
        }

        const size_t closeTagStart = markup.find("</span>", tagEnd + 1);
        if (closeTagStart == std::string::npos) {
            AppendRun(formatted, markup.substr(spanStart), false, 0);
            break;
        }

        uint32_t colorRgb = 0;
        bool hasColor = false;
        const std::string_view openTag(markup.data() + spanStart, tagEnd - spanStart + 1);
        const size_t hashIndex = openTag.find('#');
        if (hashIndex != std::string_view::npos && hashIndex + 7 <= openTag.size()) {
            hasColor = TryParseHexColor(openTag.substr(hashIndex, 7), colorRgb);
        }

        AppendRun(formatted, markup.substr(tagEnd + 1, closeTagStart - (tagEnd + 1)), hasColor, colorRgb);
        cursor = closeTagStart + std::string_view("</span>").size();
    }

    return formatted;
}

} // namespace

NinjabrainFormattedInformationMessage FormatNinjabrainInformationMessage(
    const NinjabrainInformationMessage& message) {
    return ParseMarkupToRuns(TranslateMarkupForInformationMessage(message));
}