#include "text_normalizer.h"

#include <cctype>

namespace text {

std::string TextNormalizer::ApplyOptionalPhoneticNormalization(const std::string& text) const {
    if (!options_.enable_phonetic_normalization) {
        return text;
    }

    std::string normalized;
    normalized.reserve(text.size());

    for (char ch : text) {
        // Generic leetspeak folding keeps this data-agnostic and optional.
        switch (ch) {
            case '0':
                normalized.push_back('o');
                break;
            case '1':
                normalized.push_back('i');
                break;
            case '3':
                normalized.push_back('e');
                break;
            case '5':
                normalized.push_back('s');
                break;
            case '7':
                normalized.push_back('t');
                break;
            default:
                normalized.push_back(ch);
                break;
        }
    }

    return normalized;
}

std::string TextNormalizer::NormalizeKidInput(const std::string& text) const {
    if (!options_.enable_text_normalization) {
        return text;
    }

    std::string normalized;
    normalized.reserve(text.size());

    bool prev_space = true;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!prev_space) {
                normalized.push_back(' ');
            }
            prev_space = true;
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
        prev_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return ApplyOptionalPhoneticNormalization(normalized);
}

std::string TextNormalizer::NormalizeWakePhrase(const std::string& text) const {
    if (!options_.enable_text_normalization) {
        return text;
    }

    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
            continue;
        }
        // Preserve non-ASCII bytes (e.g. CJK UTF-8 bytes) so multilingual wake words still match.
        if (ch >= 0x80) {
            normalized.push_back(static_cast<char>(ch));
        }
    }

    return ApplyOptionalPhoneticNormalization(normalized);
}

} // namespace text
