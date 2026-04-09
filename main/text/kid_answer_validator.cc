#include "kid_answer_validator.h"

#include <cctype>

namespace text {

namespace {

#ifndef CONFIG_STT_KID_ANSWER_MAX_NORMALIZED_CHARS
#define CONFIG_STT_KID_ANSWER_MAX_NORMALIZED_CHARS 12
#endif

constexpr int kKidAnswerMaxNormalizedChars = CONFIG_STT_KID_ANSWER_MAX_NORMALIZED_CHARS;

} // namespace

int KidAnswerValidator::CountWords(const std::string& text) {
    int words = 0;
    bool in_word = false;

    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            words++;
        }
    }

    return words;
}

KidAnswerValidationResult KidAnswerValidator::Validate(const std::string& raw_text, int max_expected_words) const {
    KidAnswerValidationResult result;
    result.normalized_text = normalizer_.NormalizeKidInput(raw_text);

    if (result.normalized_text.empty()) {
        return result;
    }
    if (result.normalized_text.length() > static_cast<size_t>(kKidAnswerMaxNormalizedChars)) {
        return result;
    }
    if (max_expected_words > 0 && CountWords(result.normalized_text) > max_expected_words) {
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace text
