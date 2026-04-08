#include "kid_answer_validator.h"

#include <cctype>

namespace text {

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

KidAnswerValidationResult KidAnswerValidator::Validate(const std::string& raw_text) const {
    KidAnswerValidationResult result;
    result.normalized_text = normalizer_.NormalizeKidInput(raw_text);

    if (result.normalized_text.empty()) {
        return result;
    }
    if (result.normalized_text.length() > 12) {
        return result;
    }
    if (CountWords(result.normalized_text) > 3) {
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace text
