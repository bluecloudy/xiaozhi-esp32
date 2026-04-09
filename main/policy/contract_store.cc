#include "main/policy/contract_store.h"

#include "main/policy/generated/policy_bundle_v1.h"

#include <sstream>

namespace policy {
namespace {

PolicyAction ParseAction(const char* action) {
    const std::string value = action != nullptr ? action : "";
    if (value == "reprompt") {
        return PolicyAction::kReprompt;
    }
    if (value == "abort") {
        return PolicyAction::kAbort;
    }
    if (value == "suppress") {
        return PolicyAction::kSuppress;
    }
    return PolicyAction::kContinue;
}

std::vector<std::string> SplitCsv(const char* csv) {
    std::vector<std::string> words;
    if (csv == nullptr || csv[0] == '\0') {
        return words;
    }

    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            words.push_back(token);
        }
    }
    return words;
}

}  // namespace

PolicyBundle ContractStore::LoadBundle(const RuntimeConfig& runtime_cfg) const {
    PolicyBundle bundle;

    bundle.stt.evaluation_order.assign(
        generated::kSttEvaluationOrder,
        generated::kSttEvaluationOrder + generated::kSttEvaluationOrderCount);

    bundle.stt.empty_input_decision.action = ParseAction(generated::kEmptyInputDecisionAction);
    bundle.stt.empty_input_decision.reason = generated::kEmptyInputDecisionReason;
    bundle.stt.empty_input_decision.reprompt_key = generated::kEmptyInputRepromptKey;

    if (std::string(generated::kListeningTimeoutSource) == "config_handoff" &&
        std::string(generated::kListeningTimeoutConfigKey) == "voice.listening_timeout_ticks") {
        bundle.session.listening_timeout_ticks = runtime_cfg.voice_listening_timeout_ticks;
    }
    bundle.session.timeout_action = ParseAction(generated::kTimeoutDecisionAction);
    bundle.session.timeout_reason = generated::kTimeoutDecisionReason;
    bundle.session.timeout_reprompt_key = generated::kTimeoutRepromptKey;
    bundle.session.reset_clock = generated::kTimeoutResetClock;

    bundle.intent.precedence.assign(
        generated::kIntentNames,
        generated::kIntentNames + generated::kIntentCount);

    for (std::size_t i = 0; i < generated::kIntentCount; ++i) {
        bundle.intent.keyword_sets[bundle.intent.precedence[i]] =
            SplitCsv(generated::kIntentKeywordCsv[i]);
    }

    return bundle;
}

}  // namespace policy
