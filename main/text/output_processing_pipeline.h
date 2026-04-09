#ifndef TEXT_OUTPUT_PROCESSING_PIPELINE_H_
#define TEXT_OUTPUT_PROCESSING_PIPELINE_H_

#include <memory>
#include <string>
#include <vector>

namespace text {

enum class ExpectedLanguageMode {
    VI_ONLY,
    EN_ONLY,
    BILINGUAL,
};

enum class ResponseType {
    CHAT,
    LESSON,
    REPROMPT,
    STORY,
    MEDIA_CONFIRMATION,
};

enum class OutputAction {
    CONTINUE,
    SUPPRESS,
};

enum class OutputReason {
    NONE,
    MALFORMED_TEXT,
    SYSTEM_TEXT_SUPPRESSED,
    LEARNING_FLOW_SUPPRESSED,
};

inline constexpr const char* kOutputRepromptTextKeyTryAgainSimple = "TRY_AGAIN_SIMPLE";
inline constexpr const char* kOutputRepromptTextKeyTryAgainWithName = "TRY_AGAIN_WITH_NAME";

struct OutputProcessingPipelineConfig {
    bool enable_output_normalization = true;
    bool enable_bilingual_routing = true;
    bool enable_tts_output_policy = true;
    bool enable_learning_flow_guard = true;
    bool enable_reprompt_output = true;
};

struct OutputContext {
    std::string raw_text;
    std::string normalized_text;

    bool is_learning_mode = false;
    bool is_waiting_for_answer = false;
    bool is_parent_mode = false;
    bool is_story_mode = false;

    ExpectedLanguageMode expected_language_mode = ExpectedLanguageMode::BILINGUAL;
    ResponseType response_type = ResponseType::CHAT;

    std::string reprompt_text_key;
    std::string assistant_name;
};

struct OutputResult {
    // OutputResult is policy-only: no UI/protocol payloads or side effects.
    OutputAction action = OutputAction::CONTINUE;
    OutputReason reason = OutputReason::NONE;

    static OutputResult Continue() {
        return OutputResult{};
    }

    static OutputResult Suppress(OutputReason suppress_reason) {
        OutputResult result;
        result.action = OutputAction::SUPPRESS;
        result.reason = suppress_reason;
        return result;
    }
};

class IOutputPlugin {
public:
    virtual ~IOutputPlugin() = default;
    virtual OutputResult Process(OutputContext& context) const = 0;
};

class OutputNormalizationPlugin : public IOutputPlugin {
public:
    explicit OutputNormalizationPlugin(bool enabled) : enabled_(enabled) {}
    OutputResult Process(OutputContext& context) const override;

private:
    bool enabled_ = true;
};

class BilingualRoutingPlugin : public IOutputPlugin {
public:
    explicit BilingualRoutingPlugin(bool enabled) : enabled_(enabled) {}
    OutputResult Process(OutputContext& context) const override;

private:
    bool enabled_ = true;
};

class TtsOutputPolicyPlugin : public IOutputPlugin {
public:
    explicit TtsOutputPolicyPlugin(bool enabled) : enabled_(enabled) {}
    OutputResult Process(OutputContext& context) const override;

private:
    bool enabled_ = true;
};

class RepromptOutputPlugin : public IOutputPlugin {
public:
    explicit RepromptOutputPlugin(bool enabled) : enabled_(enabled) {}
    OutputResult Process(OutputContext& context) const override;

private:
    bool enabled_ = true;
};

class LearningFlowOutputGuardPlugin : public IOutputPlugin {
public:
    explicit LearningFlowOutputGuardPlugin(bool enabled) : enabled_(enabled) {}
    OutputResult Process(OutputContext& context) const override;

private:
    bool enabled_ = true;
};

class OutputProcessingPipeline {
public:
    // Plugin order is fixed in the constructor and order-sensitive by design.
    explicit OutputProcessingPipeline(const OutputProcessingPipelineConfig& config);
    OutputResult Process(OutputContext& context) const;

private:
    void AddPlugin(std::unique_ptr<IOutputPlugin> plugin);

    std::vector<std::unique_ptr<IOutputPlugin>> plugins_;
};

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
// Runs lightweight behavior checks for ordering and key output policy decisions.
bool RunOutputPipelineSelfCheck();
#endif

} // namespace text

#endif // TEXT_OUTPUT_PROCESSING_PIPELINE_H_
