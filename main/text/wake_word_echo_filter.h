#ifndef TEXT_WAKE_WORD_ECHO_FILTER_H_
#define TEXT_WAKE_WORD_ECHO_FILTER_H_

#include <string>

#include "text_normalizer.h"

namespace text {

// Filters STT results that are likely wake-word echoes after wake trigger.
class WakeWordEchoFilter {
public:
    WakeWordEchoFilter(TextNormalizationOptions options, bool enabled)
        : normalizer_(options), enabled_(enabled) {}

    bool ShouldFilter(const std::string& incoming_text, const std::string& wake_phrase) const;

private:
    TextNormalizer normalizer_;
    bool enabled_ = true;
};

} // namespace text

#endif // TEXT_WAKE_WORD_ECHO_FILTER_H_
