#ifndef TEXT_TEXT_NORMALIZER_H_
#define TEXT_TEXT_NORMALIZER_H_

#include <string>

namespace text {

struct TextNormalizationOptions {
    bool enable_text_normalization = true;
    bool enable_phonetic_normalization = false;
};

// Generic text normalization helper used by STT/wake-word pipelines.
class TextNormalizer {
public:
    explicit TextNormalizer(TextNormalizationOptions options) : options_(options) {}

    std::string NormalizeKidInput(const std::string& text) const;
    std::string NormalizeWakePhrase(const std::string& text) const;

private:
    std::string ApplyOptionalPhoneticNormalization(const std::string& text) const;

    TextNormalizationOptions options_;
};

} // namespace text

#endif // TEXT_TEXT_NORMALIZER_H_
