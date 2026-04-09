#ifndef TEXT_TEXT_PROCESSING_PIPELINE_H_
#define TEXT_TEXT_PROCESSING_PIPELINE_H_

#include <memory>
#include <string>
#include <vector>

#include "kid_answer_validator.h"
#include "text_normalizer.h"
#include "wake_word_echo_filter.h"

namespace text {

enum class ProcessingAction {
    CONTINUE,
    IGNORE,
    REPROMPT,
    ABORT,
};

enum class ProcessingReason {
    NONE,
    WAKE_WORD_ECHO,
    INVALID_KID_ANSWER,
    NO_INPUT,
    NOISE_INPUT,
    INTENT_BLOCKED,
};

enum class UserIntent {
    ANSWER,
    COMMAND,
    SMALL_TALK,
    CANCEL,
    UNKNOWN,
};

enum class ExpectedAnswerType {
    WORD,
    SHORT_PHRASE,
    FREE_TEXT,
};

struct AnswerPolicy {
    ExpectedAnswerType expected_answer_type = ExpectedAnswerType::SHORT_PHRASE;
    int max_expected_words = 0;
    bool allow_empty = false;
    bool allow_noise_tolerance = false;
};

inline constexpr const char* kRepromptTextKeyTryAgainSimple = "TRY_AGAIN_SIMPLE";
inline constexpr const char* kRepromptTextKeyTryAgainWithName = "TRY_AGAIN_WITH_NAME";

struct TextProcessingPipelineConfig {
    TextNormalizationOptions normalization_options;
    bool enable_wake_word_echo_filter = true;
    bool enable_kid_answer_validation = true;
    bool enable_reprompt_policy = true;
};

struct ProcessingContext {
    std::string raw_text;
    std::string normalized_text;
    std::string wake_word;
    bool ignore_next_stt_after_wake = false;
    bool is_learning_mode = false;
    bool is_waiting_for_answer = false;
    UserIntent user_intent = UserIntent::UNKNOWN;
    AnswerPolicy answer_policy;
    ProcessingReason reason = ProcessingReason::NONE;
};

struct ProcessingResult {
    ProcessingAction action = ProcessingAction::CONTINUE;
    ProcessingReason reason = ProcessingReason::NONE;
    std::string reprompt_text_key;
};

class ITextPlugin {
public:
    virtual ~ITextPlugin() = default;
    virtual ProcessingResult Process(ProcessingContext& context) const = 0;
};

class TextProcessingPipeline {
public:
    explicit TextProcessingPipeline(const TextProcessingPipelineConfig& config);
    ProcessingResult Process(ProcessingContext& context) const;

private:
    void AddPlugin(std::unique_ptr<ITextPlugin> plugin);

    std::vector<std::unique_ptr<ITextPlugin>> plugins_;
};

class TextNormalizationPlugin : public ITextPlugin {
public:
    explicit TextNormalizationPlugin(TextNormalizationOptions options)
        : normalizer_(options) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    TextNormalizer normalizer_;
};

class PhoneticNormalizationPlugin : public ITextPlugin {
public:
    explicit PhoneticNormalizationPlugin(TextNormalizationOptions options)
        : normalizer_(options), options_(options) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    TextNormalizer normalizer_;
    TextNormalizationOptions options_;
};

class WakeWordEchoPlugin : public ITextPlugin {
public:
    WakeWordEchoPlugin(TextNormalizationOptions options, bool enabled)
        : echo_filter_(options, enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    WakeWordEchoFilter echo_filter_;
};

class NoInputPlugin : public ITextPlugin {
public:
    explicit NoInputPlugin(bool enabled) : enabled_(enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    bool enabled_ = true;
};

class NoiseInputPlugin : public ITextPlugin {
public:
    explicit NoiseInputPlugin(bool enabled) : enabled_(enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    bool enabled_ = true;
};

class IntentDetectionPlugin : public ITextPlugin {
public:
    ProcessingResult Process(ProcessingContext& context) const override;
};

class LearningFlowGuardPlugin : public ITextPlugin {
public:
    explicit LearningFlowGuardPlugin(bool enabled) : enabled_(enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    bool enabled_ = true;
};

class KidAnswerValidationPlugin : public ITextPlugin {
public:
    explicit KidAnswerValidationPlugin(bool enabled)
        : validator_(TextNormalizationOptions{false, false}), enabled_(enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    KidAnswerValidator validator_;
    bool enabled_ = true;
};

class RepromptPolicyPlugin : public ITextPlugin {
public:
    explicit RepromptPolicyPlugin(bool enabled) : enabled_(enabled) {}

    ProcessingResult Process(ProcessingContext& context) const override;

private:
    bool enabled_ = true;
};

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
// Runs lightweight behavior checks for pipeline ordering and key reprompt decisions.
bool RunTextPipelineSelfCheck();
#endif

} // namespace text

#endif // TEXT_TEXT_PROCESSING_PIPELINE_H_
