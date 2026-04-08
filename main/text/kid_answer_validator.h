#ifndef TEXT_KID_ANSWER_VALIDATOR_H_
#define TEXT_KID_ANSWER_VALIDATOR_H_

#include <string>

#include "text_normalizer.h"

namespace text {

struct KidAnswerValidationResult {
    bool valid = false;
    std::string normalized_text;
};

// Validates short kid-mode ASR answers after optional normalization.
class KidAnswerValidator {
public:
    explicit KidAnswerValidator(TextNormalizationOptions options)
        : normalizer_(options) {}

    KidAnswerValidationResult Validate(const std::string& raw_text) const;

private:
    static int CountWords(const std::string& text);

    TextNormalizer normalizer_;
};

} // namespace text

#endif // TEXT_KID_ANSWER_VALIDATOR_H_
