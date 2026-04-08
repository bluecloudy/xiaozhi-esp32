#include "wake_word_echo_filter.h"

namespace text {

bool WakeWordEchoFilter::ShouldFilter(const std::string& incoming_text, const std::string& wake_phrase) const {
    if (!enabled_ || incoming_text.empty() || wake_phrase.empty()) {
        return false;
    }

    auto normalized_incoming = normalizer_.NormalizeWakePhrase(incoming_text);
    auto normalized_wake = normalizer_.NormalizeWakePhrase(wake_phrase);

    if (normalized_incoming.empty() || normalized_wake.empty()) {
        return false;
    }
    if (normalized_incoming == normalized_wake) {
        return true;
    }

    return normalized_incoming.find(normalized_wake) != std::string::npos ||
           normalized_wake.find(normalized_incoming) != std::string::npos;
}

} // namespace text
