#include "text_processing_pipeline.h"

#include <algorithm>
#include <cctype>
#include <utility>
#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
#include <esp_log.h>
#endif

namespace text {

namespace {

#ifndef CONFIG_STT_NOISE_REPEATED_MIN_TOKENS
#define CONFIG_STT_NOISE_REPEATED_MIN_TOKENS 4
#endif

#ifndef CONFIG_STT_NOISE_REPEATED_DOMINANCE_PERCENT
#define CONFIG_STT_NOISE_REPEATED_DOMINANCE_PERCENT 75
#endif

#ifndef CONFIG_STT_NOISE_REPEATED_MAX_UNIQUE_TOKENS
#define CONFIG_STT_NOISE_REPEATED_MAX_UNIQUE_TOKENS 2
#endif

#ifndef CONFIG_STT_NOISE_REPEATED_MAX_TOKEN_LENGTH
#define CONFIG_STT_NOISE_REPEATED_MAX_TOKEN_LENGTH 3
#endif

#ifndef CONFIG_STT_LONG_FRAGMENT_MULTIPLIER
#define CONFIG_STT_LONG_FRAGMENT_MULTIPLIER 4
#endif

#ifndef CONFIG_STT_LONG_FRAGMENT_MARGIN_WORDS
#define CONFIG_STT_LONG_FRAGMENT_MARGIN_WORDS 8
#endif

#ifndef CONFIG_STT_LEARNING_SHORT_PHRASE_DEFAULT_MAX_WORDS
#define CONFIG_STT_LEARNING_SHORT_PHRASE_DEFAULT_MAX_WORDS 3
#endif

#ifndef CONFIG_STT_INTENT_CANCEL_KEYWORDS
#define CONFIG_STT_INTENT_CANCEL_KEYWORDS "stop,cancel,dung,thoi"
#endif

#ifndef CONFIG_STT_INTENT_COMMAND_KEYWORDS
#define CONFIG_STT_INTENT_COMMAND_KEYWORDS "repeat,again,next,skip,help,pause,resume"
#endif

#ifndef CONFIG_STT_INTENT_SMALL_TALK_KEYWORDS
#define CONFIG_STT_INTENT_SMALL_TALK_KEYWORDS "thanks,thank,hello,hi"
#endif

#ifndef CONFIG_STT_INTENT_SMALL_TALK_PHRASES
#define CONFIG_STT_INTENT_SMALL_TALK_PHRASES "cam on,xin chao,how are you"
#endif

#ifndef CONFIG_STT_LEARNING_WORD_INTENT_MAX_WORDS
#define CONFIG_STT_LEARNING_WORD_INTENT_MAX_WORDS 2
#endif

#ifndef CONFIG_STT_LEARNING_SHORT_PHRASE_INTENT_EXTRA_WORDS
#define CONFIG_STT_LEARNING_SHORT_PHRASE_INTENT_EXTRA_WORDS 1
#endif

#ifndef CONFIG_STT_NON_LEARNING_ANSWER_INTENT_MAX_WORDS
#define CONFIG_STT_NON_LEARNING_ANSWER_INTENT_MAX_WORDS 8
#endif

bool IsSemanticChar(unsigned char ch) {
    return ch >= 0x80 || std::isalnum(ch);
}

char ToLowerAscii(unsigned char ch) {
    return ch < 0x80 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch);
}

const std::string& ResolveInputText(const ProcessingContext& context) {
    return context.normalized_text.empty() ? context.raw_text : context.normalized_text;
}

constexpr int kNoiseRepeatedMinTokens = CONFIG_STT_NOISE_REPEATED_MIN_TOKENS;
constexpr int kNoiseRepeatedDominancePercent = CONFIG_STT_NOISE_REPEATED_DOMINANCE_PERCENT;
constexpr int kNoiseRepeatedMaxUniqueTokens = CONFIG_STT_NOISE_REPEATED_MAX_UNIQUE_TOKENS;
constexpr int kNoiseRepeatedMaxTokenLength = CONFIG_STT_NOISE_REPEATED_MAX_TOKEN_LENGTH;
constexpr int kLongFragmentMultiplier = CONFIG_STT_LONG_FRAGMENT_MULTIPLIER;
constexpr int kLongFragmentMarginWords = CONFIG_STT_LONG_FRAGMENT_MARGIN_WORDS;
constexpr int kShortPhraseDefaultMaxWords = CONFIG_STT_LEARNING_SHORT_PHRASE_DEFAULT_MAX_WORDS;
constexpr int kLearningWordIntentMaxWords = CONFIG_STT_LEARNING_WORD_INTENT_MAX_WORDS;
constexpr int kLearningShortPhraseIntentExtraWords = CONFIG_STT_LEARNING_SHORT_PHRASE_INTENT_EXTRA_WORDS;
constexpr int kNonLearningAnswerIntentMaxWords = CONFIG_STT_NON_LEARNING_ANSWER_INTENT_MAX_WORDS;

bool IsBlankInput(const std::string& input) {
    for (unsigned char ch : input) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

std::string ToLowerAsciiCopy(const std::string& input) {
    std::string lower;
    lower.reserve(input.size());
    for (unsigned char ch : input) {
        lower.push_back(ToLowerAscii(ch));
    }
    return lower;
}

std::string TrimAsciiCopy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(start, end - start);
}

std::vector<std::string> ParseConfiguredKeywordList(const char* raw_list) {
    std::vector<std::string> keywords;
    if (raw_list == nullptr || raw_list[0] == '\0') {
        return keywords;
    }

    std::string current;
    for (const char* p = raw_list; *p != '\0'; ++p) {
        const char ch = *p;
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\r') {
            std::string term = TrimAsciiCopy(current);
            if (!term.empty()) {
                keywords.push_back(ToLowerAsciiCopy(term));
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    std::string term = TrimAsciiCopy(current);
    if (!term.empty()) {
        keywords.push_back(ToLowerAsciiCopy(term));
    }

    return keywords;
}

const std::vector<std::string>& CancelIntentKeywords() {
    static const std::vector<std::string> keywords =
        ParseConfiguredKeywordList(CONFIG_STT_INTENT_CANCEL_KEYWORDS);
    return keywords;
}

const std::vector<std::string>& CommandIntentKeywords() {
    static const std::vector<std::string> keywords =
        ParseConfiguredKeywordList(CONFIG_STT_INTENT_COMMAND_KEYWORDS);
    return keywords;
}

const std::vector<std::string>& SmallTalkIntentKeywords() {
    static const std::vector<std::string> keywords =
        ParseConfiguredKeywordList(CONFIG_STT_INTENT_SMALL_TALK_KEYWORDS);
    return keywords;
}

const std::vector<std::string>& SmallTalkIntentPhrases() {
    static const std::vector<std::string> phrases =
        ParseConfiguredKeywordList(CONFIG_STT_INTENT_SMALL_TALK_PHRASES);
    return phrases;
}

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
std::string FirstConfiguredTermOrFallback(const std::vector<std::string>& terms,
                                          const char* fallback) {
    if (!terms.empty()) {
        return terms.front();
    }
    return fallback == nullptr ? std::string{} : std::string(fallback);
}
#endif

AnswerPolicy ResolveAnswerPolicy(const ProcessingContext& context);

std::vector<std::string> CollectSemanticTokensLowerAscii(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(input.size());

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (unsigned char ch : input) {
        if (IsSemanticChar(ch)) {
            current.push_back(ToLowerAscii(ch));
        } else {
            flush();
        }
    }
    flush();

    return tokens;
}

bool HasAnyKeyword(const std::vector<std::string>& tokens,
                   const std::vector<std::string>& keywords) {
    for (const auto& token : tokens) {
        for (const auto& keyword : keywords) {
            if (token == keyword) {
                return true;
            }
        }
    }
    return false;
}

bool ContainsAnyPhrase(const std::string& input,
                       const std::vector<std::string>& phrases) {
    for (const auto& phrase : phrases) {
        if (phrase.empty()) {
            continue;
        }
        if (input.find(phrase) != std::string::npos) {
            return true;
        }
    }
    return false;
}

UserIntent DetectIntent(const ProcessingContext& context) {
    const std::string& input = ResolveInputText(context);
    if (IsBlankInput(input)) {
        return UserIntent::UNKNOWN;
    }

    const std::string lower = ToLowerAsciiCopy(input);
    const auto tokens = CollectSemanticTokensLowerAscii(lower);
    const int word_count = static_cast<int>(tokens.size());

    const auto& cancel_keywords = CancelIntentKeywords();
    const auto& command_keywords = CommandIntentKeywords();
    const auto& small_talk_keywords = SmallTalkIntentKeywords();
    const auto& small_talk_phrases = SmallTalkIntentPhrases();

    if (HasAnyKeyword(tokens, cancel_keywords) ||
        ContainsAnyPhrase(lower, cancel_keywords)) {
        return UserIntent::CANCEL;
    }

    if (HasAnyKeyword(tokens, command_keywords) ||
        ContainsAnyPhrase(lower, command_keywords)) {
        return UserIntent::COMMAND;
    }

    if (HasAnyKeyword(tokens, small_talk_keywords) ||
        ContainsAnyPhrase(lower, small_talk_phrases)) {
        return UserIntent::SMALL_TALK;
    }

    if (context.is_learning_mode) {
        const auto policy = ResolveAnswerPolicy(context);
        if (policy.expected_answer_type == ExpectedAnswerType::FREE_TEXT) {
            return UserIntent::ANSWER;
        }
        if (policy.expected_answer_type == ExpectedAnswerType::WORD) {
            return word_count > 0 && word_count <= kLearningWordIntentMaxWords
                ? UserIntent::ANSWER
                : UserIntent::UNKNOWN;
        }

        const int expected_words = policy.max_expected_words > 0
            ? policy.max_expected_words
            : kShortPhraseDefaultMaxWords;
        return word_count > 0 && word_count <= (expected_words + kLearningShortPhraseIntentExtraWords)
            ? UserIntent::ANSWER
            : UserIntent::UNKNOWN;
    }

    return word_count > 0 && word_count <= kNonLearningAnswerIntentMaxWords
        ? UserIntent::ANSWER
        : UserIntent::UNKNOWN;
}

int CountWordLikeTokens(const std::string& input) {
    int token_count = 0;
    bool in_token = false;
    for (unsigned char ch : input) {
        if (IsSemanticChar(ch)) {
            if (!in_token) {
                ++token_count;
                in_token = true;
            }
        } else {
            in_token = false;
        }
    }
    return token_count;
}

std::string CanonicalizeSemanticToken(const std::string& token) {
    std::string canonical;
    canonical.reserve(token.size());
    for (unsigned char ch : token) {
        if (IsSemanticChar(ch)) {
            canonical.push_back(ToLowerAscii(ch));
        }
    }
    return canonical;
}

bool HasDigits(const std::string& token) {
    for (unsigned char ch : token) {
        if (std::isdigit(ch)) {
            return true;
        }
    }
    return false;
}

bool IsRepeatedCharToken(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    const char first = token.front();
    for (char ch : token) {
        if (ch != first) {
            return false;
        }
    }
    return true;
}

bool IsRepeatedShortLowInfoNoise(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(input.size());

    auto flush_token = [&]() {
        if (!current.empty()) {
            std::string canonical = CanonicalizeSemanticToken(current);
            if (!canonical.empty()) {
                tokens.push_back(std::move(canonical));
            }
            current.clear();
        }
    };

    for (unsigned char ch : input) {
        if (std::isspace(ch)) {
            flush_token();
            continue;
        }
        current.push_back(static_cast<char>(ch));
    }
    flush_token();

    if (tokens.size() < static_cast<size_t>(kNoiseRepeatedMinTokens)) {
        return false;
    }

    struct TokenCount {
        std::string token;
        int count = 0;
    };

    std::vector<TokenCount> frequencies;
    int max_count = 0;
    int max_token_len = 0;
    bool has_digit = false;
    bool has_low_information_token = false;

    for (const auto& token : tokens) {
        max_token_len = std::max(max_token_len, static_cast<int>(token.size()));
        has_digit = has_digit || HasDigits(token);
        has_low_information_token = has_low_information_token || token.size() == 1 || IsRepeatedCharToken(token);

        bool found = false;
        for (auto& entry : frequencies) {
            if (entry.token == token) {
                ++entry.count;
                max_count = std::max(max_count, entry.count);
                found = true;
                break;
            }
        }
        if (!found) {
            frequencies.push_back(TokenCount{token, 1});
            max_count = std::max(max_count, 1);
        }
    }

    const int token_count = static_cast<int>(tokens.size());
    const bool mostly_repeated =
        (max_count * 100) >= (token_count * kNoiseRepeatedDominancePercent);
    return mostly_repeated
        && frequencies.size() <= static_cast<size_t>(kNoiseRepeatedMaxUniqueTokens)
        && max_token_len <= kNoiseRepeatedMaxTokenLength
        && has_low_information_token
        && !has_digit;
}

bool IsStrictLongOffTargetFragment(const std::string& input,
                                   const ProcessingContext& context,
                                   const AnswerPolicy& policy) {
    if (!context.is_learning_mode || policy.allow_noise_tolerance || policy.max_expected_words <= 0) {
        return false;
    }
    if (policy.expected_answer_type == ExpectedAnswerType::FREE_TEXT) {
        return false;
    }

    const int word_count = CountWordLikeTokens(input);
    const int long_fragment_threshold = std::max(policy.max_expected_words * kLongFragmentMultiplier,
                                                 policy.max_expected_words + kLongFragmentMarginWords);
    return word_count >= long_fragment_threshold;
}

bool IsLikelyNoiseInput(const std::string& input,
                        const ProcessingContext& context,
                        const AnswerPolicy& policy) {
    int non_space_chars = 0;
    int punctuation_chars = 0;
    bool has_semantic_chars = false;

    for (unsigned char ch : input) {
        if (std::isspace(ch)) {
            continue;
        }

        ++non_space_chars;
        if (IsSemanticChar(ch)) {
            has_semantic_chars = true;
            break;
        }
        if (std::ispunct(ch)) {
            ++punctuation_chars;
        }
    }

    if (non_space_chars > 0 && !has_semantic_chars && punctuation_chars == non_space_chars) {
        return true;
    }

    if (IsRepeatedShortLowInfoNoise(input)) {
        return true;
    }

    return IsStrictLongOffTargetFragment(input, context, policy);
}

AnswerPolicy ResolveAnswerPolicy(const ProcessingContext& context) {
    AnswerPolicy policy = context.answer_policy;

    if (!context.is_learning_mode) {
        // Preserve existing non-learning behavior by allowing empty/noise outside strict answer mode.
        policy.allow_empty = true;
        policy.allow_noise_tolerance = true;
    }

    switch (policy.expected_answer_type) {
        case ExpectedAnswerType::WORD:
            policy.max_expected_words = 1;
            break;
        case ExpectedAnswerType::SHORT_PHRASE:
            if (policy.max_expected_words <= 0) {
                policy.max_expected_words = kShortPhraseDefaultMaxWords;
            }
            break;
        case ExpectedAnswerType::FREE_TEXT:
            policy.max_expected_words = 0;
            break;
    }

    return policy;
}

} // namespace

TextProcessingPipeline::TextProcessingPipeline(const TextProcessingPipelineConfig& config) {
    AddPlugin(std::make_unique<TextNormalizationPlugin>(config.normalization_options));

    if (config.normalization_options.enable_phonetic_normalization) {
        AddPlugin(std::make_unique<PhoneticNormalizationPlugin>(config.normalization_options));
    }

    // Plugin order is intentional and order-sensitive.
    // Keep in sync with RunTextPipelineSelfCheck() assertions.
    AddPlugin(std::make_unique<WakeWordEchoPlugin>(
        config.normalization_options,
        config.enable_wake_word_echo_filter));
    AddPlugin(std::make_unique<NoInputPlugin>(config.enable_reprompt_policy));
    AddPlugin(std::make_unique<NoiseInputPlugin>(config.enable_reprompt_policy));
    AddPlugin(std::make_unique<IntentDetectionPlugin>());
    AddPlugin(std::make_unique<LearningFlowGuardPlugin>(config.enable_reprompt_policy));
    AddPlugin(std::make_unique<KidAnswerValidationPlugin>(config.enable_kid_answer_validation));
    AddPlugin(std::make_unique<RepromptPolicyPlugin>(config.enable_reprompt_policy));
}

void TextProcessingPipeline::AddPlugin(std::unique_ptr<ITextPlugin> plugin) {
    if (plugin) {
        plugins_.push_back(std::move(plugin));
    }
}

ProcessingResult TextProcessingPipeline::Process(ProcessingContext& context) const {
    ProcessingResult pending_result;
    bool has_pending_reprompt = false;
    context.reason = ProcessingReason::NONE;

    for (const auto& plugin : plugins_) {
        ProcessingResult result = plugin->Process(context);

        if (result.reason != ProcessingReason::NONE) {
            context.reason = result.reason;
        }

        if (result.action == ProcessingAction::CONTINUE) {
            continue;
        }

        // Allow policy plugins to fill details for reprompt decisions.
        if (result.action == ProcessingAction::REPROMPT && result.reprompt_text_key.empty()) {
            has_pending_reprompt = true;
            pending_result = result;
            continue;
        }

        return result;
    }

    if (has_pending_reprompt) {
        pending_result.action = ProcessingAction::REPROMPT;
        if (pending_result.reason == ProcessingReason::NONE) {
            pending_result.reason = context.reason;
        }
        return pending_result;
    }

    return ProcessingResult{};
}

ProcessingResult TextNormalizationPlugin::Process(ProcessingContext& context) const {
    const std::string& input = ResolveInputText(context);
    context.normalized_text = normalizer_.NormalizeKidInput(input);
    return ProcessingResult{};
}

ProcessingResult PhoneticNormalizationPlugin::Process(ProcessingContext& context) const {
    if (!options_.enable_phonetic_normalization) {
        return ProcessingResult{};
    }

    const std::string& input = ResolveInputText(context);
    context.normalized_text = normalizer_.NormalizeKidInput(input);
    return ProcessingResult{};
}

ProcessingResult WakeWordEchoPlugin::Process(ProcessingContext& context) const {
    if (!context.ignore_next_stt_after_wake) {
        return ProcessingResult{};
    }

    // Consume one-shot wake-word echo protection immediately to avoid cross-turn leakage.
    context.ignore_next_stt_after_wake = false;

    const std::string& input = ResolveInputText(context);
    if (!echo_filter_.ShouldFilter(input, context.wake_word)) {
        return ProcessingResult{};
    }

    ProcessingResult result;
    result.action = ProcessingAction::IGNORE;
    result.reason = ProcessingReason::WAKE_WORD_ECHO;
    return result;
}

ProcessingResult NoInputPlugin::Process(ProcessingContext& context) const {
    if (!enabled_) {
        return ProcessingResult{};
    }

    const auto policy = ResolveAnswerPolicy(context);
    if (policy.allow_empty) {
        return ProcessingResult{};
    }

    const std::string& input = ResolveInputText(context);
    if (!IsBlankInput(input)) {
        return ProcessingResult{};
    }

    ProcessingResult result;
    result.action = ProcessingAction::REPROMPT;
    result.reason = ProcessingReason::NO_INPUT;
    return result;
}

ProcessingResult NoiseInputPlugin::Process(ProcessingContext& context) const {
    if (!enabled_) {
        return ProcessingResult{};
    }

    const auto policy = ResolveAnswerPolicy(context);
    if (policy.allow_noise_tolerance) {
        return ProcessingResult{};
    }

    const std::string& input = ResolveInputText(context);
    if (IsBlankInput(input) || !IsLikelyNoiseInput(input, context, policy)) {
        return ProcessingResult{};
    }

    ProcessingResult result;
    result.action = ProcessingAction::REPROMPT;
    result.reason = ProcessingReason::NOISE_INPUT;
    return result;
}

ProcessingResult IntentDetectionPlugin::Process(ProcessingContext& context) const {
    context.user_intent = DetectIntent(context);
    return ProcessingResult{};
}

ProcessingResult LearningFlowGuardPlugin::Process(ProcessingContext& context) const {
    if (!enabled_ || !context.is_learning_mode || !context.is_waiting_for_answer) {
        return ProcessingResult{};
    }

    if (context.user_intent == UserIntent::ANSWER) {
        return ProcessingResult{};
    }

    ProcessingResult result;
    result.reason = ProcessingReason::INTENT_BLOCKED;
    if (context.user_intent == UserIntent::CANCEL) {
        result.action = ProcessingAction::ABORT;
        return result;
    }

    result.action = ProcessingAction::REPROMPT;
    return result;
}

ProcessingResult KidAnswerValidationPlugin::Process(ProcessingContext& context) const {
    if (!enabled_ || !context.is_learning_mode) {
        return ProcessingResult{};
    }

    if (context.user_intent != UserIntent::ANSWER) {
        return ProcessingResult{};
    }

    const std::string& input = ResolveInputText(context);
    if (IsBlankInput(input)) {
        return ProcessingResult{};
    }

    const auto policy = ResolveAnswerPolicy(context);
    if (policy.max_expected_words <= 0) {
        return ProcessingResult{};
    }

    auto validation = validator_.Validate(input, policy.max_expected_words);

    if (!validation.valid) {
        ProcessingResult result;
        result.action = ProcessingAction::REPROMPT;
        result.reason = ProcessingReason::INVALID_KID_ANSWER;
        return result;
    }

    if (!validation.normalized_text.empty()) {
        context.normalized_text = validation.normalized_text;
    }

    return ProcessingResult{};
}

ProcessingResult RepromptPolicyPlugin::Process(ProcessingContext& context) const {
    if (!enabled_ || context.reason == ProcessingReason::NONE) {
        return ProcessingResult{};
    }

    if (context.reason == ProcessingReason::INVALID_KID_ANSWER) {
        ProcessingResult result;
        result.action = ProcessingAction::REPROMPT;
        result.reason = context.reason;
        result.reprompt_text_key = kRepromptTextKeyTryAgainWithName;
        return result;
    }

    if (context.reason == ProcessingReason::NO_INPUT) {
        ProcessingResult result;
        result.action = ProcessingAction::REPROMPT;
        result.reason = context.reason;
        result.reprompt_text_key = kRepromptTextKeyTryAgainSimple;
        return result;
    }

    if (context.reason == ProcessingReason::NOISE_INPUT) {
        ProcessingResult result;
        result.action = ProcessingAction::REPROMPT;
        result.reason = context.reason;
        result.reprompt_text_key = kRepromptTextKeyTryAgainSimple;
        return result;
    }

    if (context.reason == ProcessingReason::INTENT_BLOCKED) {
        ProcessingResult result;
        result.action = ProcessingAction::REPROMPT;
        result.reason = context.reason;
        result.reprompt_text_key = kRepromptTextKeyTryAgainSimple;
        return result;
    }

    return ProcessingResult{};
}

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
namespace {

constexpr const char* kSelfCheckTag = "TextPipelineSelfCheck";

ProcessingContext MakeLearningContext(const std::string& raw_text,
                                      ExpectedAnswerType expected_type,
                                      int max_expected_words,
                                      bool waiting_for_answer = false) {
    ProcessingContext context;
    context.raw_text = raw_text;
    context.normalized_text = raw_text;
    context.is_learning_mode = true;
    context.is_waiting_for_answer = waiting_for_answer;
    context.answer_policy.expected_answer_type = expected_type;
    context.answer_policy.max_expected_words = max_expected_words;
    return context;
}

bool VerifyCase(const char* name,
                const TextProcessingPipeline& pipeline,
                ProcessingContext context,
                ProcessingAction expected_action,
                ProcessingReason expected_reason,
                const char* expected_reprompt_key) {
    const ProcessingResult result = pipeline.Process(context);
    const bool action_ok = result.action == expected_action;
    const bool reason_ok = result.reason == expected_reason;
    const bool key_ok = result.reprompt_text_key == expected_reprompt_key;

    if (!action_ok || !reason_ok || !key_ok) {
        ESP_LOGE(kSelfCheckTag,
                 "case=%s failed action=%d/%d reason=%d/%d key='%s'/'%s'",
                 name,
                 static_cast<int>(result.action),
                 static_cast<int>(expected_action),
                 static_cast<int>(result.reason),
                 static_cast<int>(expected_reason),
                 result.reprompt_text_key.c_str(),
                 expected_reprompt_key);
        return false;
    }
    return true;
}

} // namespace

bool RunTextPipelineSelfCheck() {
    TextProcessingPipelineConfig config;
    config.normalization_options.enable_text_normalization = false;
    config.normalization_options.enable_phonetic_normalization = false;
    config.enable_wake_word_echo_filter = true;
    config.enable_kid_answer_validation = true;
    config.enable_reprompt_policy = true;

    const TextProcessingPipeline pipeline(config);
    const std::string small_talk_probe =
        FirstConfiguredTermOrFallback(SmallTalkIntentKeywords(), "thanks");
    const std::string cancel_probe =
        FirstConfiguredTermOrFallback(CancelIntentKeywords(), "stop");

    bool all_ok = true;

    all_ok = VerifyCase("punctuation_noise",
                        pipeline,
                        MakeLearningContext("!!!???", ExpectedAnswerType::SHORT_PHRASE, 3),
                        ProcessingAction::REPROMPT,
                        ProcessingReason::NOISE_INPUT,
                        kRepromptTextKeyTryAgainSimple) && all_ok;

    // Order-sensitive: noise should be rejected before kid-answer length validation.
    all_ok = VerifyCase("repeated_short_noise_before_validation",
                        pipeline,
                        MakeLearningContext("mm mm mm mm", ExpectedAnswerType::SHORT_PHRASE, 3),
                        ProcessingAction::REPROMPT,
                        ProcessingReason::NOISE_INPUT,
                        kRepromptTextKeyTryAgainSimple) && all_ok;

    all_ok = VerifyCase("strict_long_fragment_noise",
                        pipeline,
                        MakeLearningContext("this answer keeps going and includes many extra unrelated words for this mode",
                                            ExpectedAnswerType::SHORT_PHRASE,
                                            3),
                        ProcessingAction::REPROMPT,
                        ProcessingReason::NOISE_INPUT,
                        kRepromptTextKeyTryAgainSimple) && all_ok;

    all_ok = VerifyCase("answer_validation_still_applies",
                        pipeline,
                        MakeLearningContext("this answer has four words", ExpectedAnswerType::WORD, 1),
                        ProcessingAction::REPROMPT,
                        ProcessingReason::INVALID_KID_ANSWER,
                        kRepromptTextKeyTryAgainWithName) && all_ok;

    all_ok = VerifyCase("learning_small_talk_blocked",
                        pipeline,
                        MakeLearningContext(small_talk_probe, ExpectedAnswerType::SHORT_PHRASE, 3, true),
                        ProcessingAction::REPROMPT,
                        ProcessingReason::INTENT_BLOCKED,
                        kRepromptTextKeyTryAgainSimple) && all_ok;

    all_ok = VerifyCase("learning_cancel_aborts",
                        pipeline,
                        MakeLearningContext(cancel_probe, ExpectedAnswerType::SHORT_PHRASE, 3, true),
                        ProcessingAction::ABORT,
                        ProcessingReason::INTENT_BLOCKED,
                        "") && all_ok;

    if (all_ok) {
        ESP_LOGI(kSelfCheckTag, "all checks passed");
    }

    return all_ok;
}
#endif

} // namespace text
