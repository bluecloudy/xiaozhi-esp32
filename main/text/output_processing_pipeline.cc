#include "output_processing_pipeline.h"

#include "assets/lang_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
#include <esp_log.h>
#endif

namespace text {

namespace {

enum class OutputPluginStage {
    BILINGUAL_ROUTING,
    NORMALIZATION,
    REPROMPT_MAPPING,
    LEARNING_FLOW_GUARD,
    TTS_OUTPUT_POLICY,
};

constexpr OutputPluginStage kFixedPluginOrder[] = {
    OutputPluginStage::BILINGUAL_ROUTING,
    OutputPluginStage::NORMALIZATION,
    OutputPluginStage::REPROMPT_MAPPING,
    OutputPluginStage::LEARNING_FLOW_GUARD,
    OutputPluginStage::TTS_OUTPUT_POLICY,
};

const std::string& ResolveInputText(const OutputContext& context) {
    return context.normalized_text.empty() ? context.raw_text : context.normalized_text;
}

bool IsBlankText(const std::string& input) {
    for (unsigned char ch : input) {
        if (!std::isspace(ch)) {
            return false;
        }
    }
    return true;
}

std::string Trim(const std::string& input) {
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

char ToLowerAscii(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return uch < 0x80 ? static_cast<char>(std::tolower(uch)) : ch;
}

bool StartsWithIgnoreCaseAscii(std::string_view input, std::string_view prefix) {
    if (input.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (ToLowerAscii(input[i]) != ToLowerAscii(prefix[i])) {
            return false;
        }
    }
    return true;
}

std::string CollapseWhitespaceAndControls(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    bool previous_space = false;
    for (unsigned char ch : input) {
        if (std::isspace(ch)) {
            if (!previous_space) {
                output.push_back(' ');
                previous_space = true;
            }
            continue;
        }

        if (ch < 0x20) {
            continue;
        }

        output.push_back(static_cast<char>(ch));
        previous_space = false;
    }

    return Trim(output);
}

std::string StripSystemStylePrefix(const std::string& input) {
    std::string trimmed = Trim(input);
    if (trimmed.empty()) {
        return trimmed;
    }

    struct Prefix {
        const char* value;
    };

    static const Prefix kPrefixes[] = {
        {"assistant:"},
        {"assistant："},
        {"system:"},
        {"system："},
        {"[assistant]"},
        {"[system]"},
    };

    std::string_view view(trimmed);
    for (const auto& prefix : kPrefixes) {
        std::string_view prefix_view(prefix.value);
        if (StartsWithIgnoreCaseAscii(view, prefix_view)) {
            return Trim(trimmed.substr(prefix_view.size()));
        }
    }

    return trimmed;
}

bool TryExtractLabeledLine(std::string_view line,
                           std::string_view label,
                           std::string* extracted_value) {
    std::string trimmed = Trim(std::string(line));
    if (trimmed.empty()) {
        return false;
    }

    std::string_view view(trimmed);
    if (view.size() >= label.size() + 2 &&
        view[0] == '[' &&
        view[label.size() + 1] == ']' &&
        StartsWithIgnoreCaseAscii(view.substr(1), label)) {
        *extracted_value = Trim(trimmed.substr(label.size() + 2));
        return !extracted_value->empty();
    }

    if (StartsWithIgnoreCaseAscii(view, label)) {
        const size_t idx = label.size();
        if (view.size() == idx) {
            return false;
        }

        const char separator = view[idx];
        if (separator == ':' || separator == '-' || separator == ' ') {
            *extracted_value = Trim(trimmed.substr(idx + 1));
            return !extracted_value->empty();
        }
    }

    return false;
}

std::string ExtractLanguageSegment(const std::string& input, std::string_view label) {
    std::string aggregated;
    size_t start = 0;

    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        if (end == std::string::npos) {
            end = input.size();
        }

        std::string extracted;
        if (TryExtractLabeledLine(std::string_view(input).substr(start, end - start), label, &extracted)) {
            if (!aggregated.empty()) {
                aggregated.push_back(' ');
            }
            aggregated += extracted;
        }

        if (end == input.size()) {
            break;
        }
        start = end + 1;
    }

    return Trim(aggregated);
}

bool LooksLikeStructuredPayload(const std::string& input) {
    const std::string trimmed = Trim(input);
    if (trimmed.empty()) {
        return false;
    }

    if (trimmed.size() >= 2) {
        const char first = trimmed.front();
        const char last = trimmed.back();
        if ((first == '{' && last == '}') ||
            (first == '[' && last == ']')) {
            return true;
        }
    }

    if (StartsWithIgnoreCaseAscii(trimmed, "```") ||
        StartsWithIgnoreCaseAscii(trimmed, "system:") ||
        StartsWithIgnoreCaseAscii(trimmed, "[system]")) {
        return true;
    }

    return false;
}

bool IsSentenceDelimiter(char ch) {
    return ch == '.' || ch == '!' || ch == '?';
}

std::string KeepFirstSentence(const std::string& input) {
    std::string trimmed = Trim(input);
    if (trimmed.empty()) {
        return trimmed;
    }

    for (size_t i = 0; i < trimmed.size(); ++i) {
        if (IsSentenceDelimiter(trimmed[i])) {
            return Trim(trimmed.substr(0, i + 1));
        }
    }

    return trimmed;
}

} // namespace

OutputResult OutputNormalizationPlugin::Process(OutputContext& context) const {
    if (!enabled_) {
        return OutputResult::Continue();
    }

    const std::string& input = ResolveInputText(context);
    std::string normalized = CollapseWhitespaceAndControls(input);
    normalized = StripSystemStylePrefix(normalized);
    context.normalized_text = normalized;
    return OutputResult::Continue();
}

OutputResult BilingualRoutingPlugin::Process(OutputContext& context) const {
    if (!enabled_ || context.expected_language_mode == ExpectedLanguageMode::BILINGUAL) {
        return OutputResult::Continue();
    }

    const std::string& input = ResolveInputText(context);
    if (input.empty()) {
        return OutputResult::Continue();
    }

    const std::string vi_segment = ExtractLanguageSegment(input, "vi");
    const std::string en_segment = ExtractLanguageSegment(input, "en");

    if (context.expected_language_mode == ExpectedLanguageMode::VI_ONLY && !vi_segment.empty()) {
        context.normalized_text = vi_segment;
    } else if (context.expected_language_mode == ExpectedLanguageMode::EN_ONLY && !en_segment.empty()) {
        context.normalized_text = en_segment;
    }

    return OutputResult::Continue();
}

OutputResult TtsOutputPolicyPlugin::Process(OutputContext& context) const {
    if (!enabled_) {
        return OutputResult::Continue();
    }

    const std::string& input = ResolveInputText(context);
    if (IsBlankText(input)) {
        return OutputResult::Suppress(OutputReason::MALFORMED_TEXT);
    }

    if (LooksLikeStructuredPayload(input)) {
        return OutputResult::Suppress(OutputReason::SYSTEM_TEXT_SUPPRESSED);
    }

    return OutputResult::Continue();
}

OutputResult RepromptOutputPlugin::Process(OutputContext& context) const {
    if (!enabled_ || context.response_type != ResponseType::REPROMPT) {
        return OutputResult::Continue();
    }

    if (!IsBlankText(ResolveInputText(context))) {
        return OutputResult::Continue();
    }

    if (context.reprompt_text_key == kOutputRepromptTextKeyTryAgainSimple) {
        context.normalized_text = Lang::Strings::REPROMPT_SHORT;
        return OutputResult::Continue();
    }

    if (context.reprompt_text_key == kOutputRepromptTextKeyTryAgainWithName) {
        char buffer[256] = {0};
        const char* name = context.assistant_name.empty()
            ? Lang::Strings::ASSISTANT_NAME
            : context.assistant_name.c_str();
        std::snprintf(buffer, sizeof(buffer), Lang::Strings::REPROMPT_NAME, name);
        context.normalized_text = buffer;
        return OutputResult::Continue();
    }

    // Keep default behavior stable if key is absent/unknown.
    context.normalized_text = Lang::Strings::REPROMPT_SHORT;
    return OutputResult::Continue();
}

OutputResult LearningFlowOutputGuardPlugin::Process(OutputContext& context) const {
    if (!enabled_ || !context.is_learning_mode) {
        return OutputResult::Continue();
    }

    if (context.is_waiting_for_answer && context.response_type != ResponseType::REPROMPT) {
        return OutputResult::Suppress(OutputReason::LEARNING_FLOW_SUPPRESSED);
    }

    if (context.response_type != ResponseType::LESSON) {
        return OutputResult::Continue();
    }

    const std::string& input = ResolveInputText(context);
    if (input.empty()) {
        return OutputResult::Continue();
    }

    std::string one_sentence = KeepFirstSentence(input);
    if (one_sentence.empty()) {
        return OutputResult::Suppress(OutputReason::LEARNING_FLOW_SUPPRESSED);
    }

    context.normalized_text = std::move(one_sentence);
    return OutputResult::Continue();
}

OutputProcessingPipeline::OutputProcessingPipeline(const OutputProcessingPipelineConfig& config) {
    // Keep this order fixed. Multiple self-check cases depend on it.
    for (OutputPluginStage stage : kFixedPluginOrder) {
        switch (stage) {
            case OutputPluginStage::BILINGUAL_ROUTING:
                AddPlugin(std::make_unique<BilingualRoutingPlugin>(config.enable_bilingual_routing));
                break;
            case OutputPluginStage::NORMALIZATION:
                AddPlugin(std::make_unique<OutputNormalizationPlugin>(config.enable_output_normalization));
                break;
            case OutputPluginStage::REPROMPT_MAPPING:
                AddPlugin(std::make_unique<RepromptOutputPlugin>(config.enable_reprompt_output));
                break;
            case OutputPluginStage::LEARNING_FLOW_GUARD:
                AddPlugin(std::make_unique<LearningFlowOutputGuardPlugin>(config.enable_learning_flow_guard));
                break;
            case OutputPluginStage::TTS_OUTPUT_POLICY:
                AddPlugin(std::make_unique<TtsOutputPolicyPlugin>(config.enable_tts_output_policy));
                break;
        }
    }
}

void OutputProcessingPipeline::AddPlugin(std::unique_ptr<IOutputPlugin> plugin) {
    if (plugin) {
        plugins_.push_back(std::move(plugin));
    }
}

OutputResult OutputProcessingPipeline::Process(OutputContext& context) const {
    for (const auto& plugin : plugins_) {
        OutputResult result = plugin->Process(context);
        if (result.action != OutputAction::CONTINUE) {
            return result;
        }
    }
    return OutputResult::Continue();
}

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
namespace {

constexpr const char* kSelfCheckTag = "OutputPipelineSelfCheck";

bool VerifyCase(const char* case_name,
                const OutputProcessingPipeline& pipeline,
                OutputContext context,
                OutputAction expected_action,
                OutputReason expected_reason,
                const char* expected_exact_text,
                const char* expected_contains_text) {
    const OutputResult result = pipeline.Process(context);
    const bool action_ok = result.action == expected_action;
    const bool reason_ok = result.reason == expected_reason;

    bool text_ok = true;
    if (expected_exact_text != nullptr) {
        text_ok = context.normalized_text == expected_exact_text;
    } else if (expected_contains_text != nullptr) {
        text_ok = context.normalized_text.find(expected_contains_text) != std::string::npos;
    }

    if (!action_ok || !reason_ok || !text_ok) {
        ESP_LOGE(kSelfCheckTag,
                 "case=%s failed action=%d/%d reason=%d/%d text='%s'",
                 case_name,
                 static_cast<int>(result.action),
                 static_cast<int>(expected_action),
                 static_cast<int>(result.reason),
                 static_cast<int>(expected_reason),
                 context.normalized_text.c_str());
        return false;
    }

    return true;
}

bool CheckRepromptSimpleMapping(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::REPROMPT;
    context.reprompt_text_key = kOutputRepromptTextKeyTryAgainSimple;
    context.assistant_name = Lang::Strings::ASSISTANT_NAME;

    // Order-sensitive: reprompt mapping must run before TTS suppression.
    return VerifyCase("reprompt_simple_mapping",
                      pipeline,
                      context,
                      OutputAction::CONTINUE,
                      OutputReason::NONE,
                      Lang::Strings::REPROMPT_SHORT,
                      nullptr);
}

bool CheckRepromptWithNameMapping(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::REPROMPT;
    context.reprompt_text_key = kOutputRepromptTextKeyTryAgainWithName;
    context.assistant_name = Lang::Strings::ASSISTANT_NAME;

    return VerifyCase("reprompt_with_name_mapping",
                      pipeline,
                      context,
                      OutputAction::CONTINUE,
                      OutputReason::NONE,
                      nullptr,
                      context.assistant_name.c_str());
}

bool CheckStructuredSuppression(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::CHAT;
    context.raw_text = "{\"type\":\"debug\"}";
    context.normalized_text = context.raw_text;

    return VerifyCase("structured_payload_suppression",
                      pipeline,
                      context,
                      OutputAction::SUPPRESS,
                      OutputReason::SYSTEM_TEXT_SUPPRESSED,
                      nullptr,
                      nullptr);
}

bool CheckEmptySuppression(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::CHAT;
    context.raw_text = "  \n\t  ";
    context.normalized_text = context.raw_text;

    return VerifyCase("empty_output_suppression",
                      pipeline,
                      context,
                      OutputAction::SUPPRESS,
                      OutputReason::MALFORMED_TEXT,
                      nullptr,
                      nullptr);
}

bool CheckBilingualRouting(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::LESSON;
    context.expected_language_mode = ExpectedLanguageMode::VI_ONLY;
    context.raw_text = "VI: xin chao\nEN: hello";
    context.normalized_text = context.raw_text;

    // Order-sensitive: routing must happen before global normalization collapse.
    return VerifyCase("bilingual_labeled_routing",
                      pipeline,
                      context,
                      OutputAction::CONTINUE,
                      OutputReason::NONE,
                      "xin chao",
                      nullptr);
}

bool CheckLearningFlowSuppressesExtraSentence(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::LESSON;
    context.is_learning_mode = true;
    context.is_waiting_for_answer = true;
    context.raw_text = "this should not be spoken now";
    context.normalized_text = context.raw_text;

    return VerifyCase("learning_flow_waiting_suppression",
                      pipeline,
                      context,
                      OutputAction::SUPPRESS,
                      OutputReason::LEARNING_FLOW_SUPPRESSED,
                      nullptr,
                      nullptr);
}

bool CheckLearningFlowSingleSentence(const OutputProcessingPipeline& pipeline) {
    OutputContext context;
    context.response_type = ResponseType::LESSON;
    context.is_learning_mode = true;
    context.raw_text = "teach this first. then ask another thing?";
    context.normalized_text = context.raw_text;

    return VerifyCase("learning_flow_single_sentence",
                      pipeline,
                      context,
                      OutputAction::CONTINUE,
                      OutputReason::NONE,
                      "teach this first.",
                      nullptr);
}

} // namespace

bool RunOutputPipelineSelfCheck() {
    OutputProcessingPipelineConfig config;
    config.enable_output_normalization = true;
    config.enable_bilingual_routing = true;
    config.enable_tts_output_policy = true;
    config.enable_reprompt_output = true;

    const OutputProcessingPipeline pipeline(config);

    bool all_ok = true;
    all_ok = CheckEmptySuppression(pipeline) && all_ok;
    all_ok = CheckStructuredSuppression(pipeline) && all_ok;
    all_ok = CheckRepromptSimpleMapping(pipeline) && all_ok;
    all_ok = CheckRepromptWithNameMapping(pipeline) && all_ok;
    all_ok = CheckBilingualRouting(pipeline) && all_ok;
    all_ok = CheckLearningFlowSuppressesExtraSentence(pipeline) && all_ok;
    all_ok = CheckLearningFlowSingleSentence(pipeline) && all_ok;

    if (all_ok) {
        ESP_LOGI(kSelfCheckTag, "all checks passed");
    }
    return all_ok;
}
#endif

} // namespace text
