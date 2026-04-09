#ifndef POLICY_POLICY_TYPES_H_
#define POLICY_POLICY_TYPES_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace policy {

enum class PolicyAction { kContinue, kReprompt, kAbort, kSuppress };

struct RuntimeConfig {
    int voice_listening_timeout_ticks = 5;
};

struct SttRuleDecision {
    PolicyAction action = PolicyAction::kContinue;
    std::string reason;
    std::string reprompt_key;
};

struct SttPolicy {
    std::vector<std::string> evaluation_order;
    SttRuleDecision empty_input_decision;
};

struct SessionPolicy {
    int listening_timeout_ticks = 0;
    PolicyAction timeout_action = PolicyAction::kContinue;
    std::string timeout_reason;
    std::string timeout_reprompt_key;
    bool reset_clock = false;
};

struct IntentPolicy {
    std::vector<std::string> precedence;
    std::unordered_map<std::string, std::vector<std::string>> keyword_sets;
};

struct PolicyBundle {
    SttPolicy stt;
    SessionPolicy session;
    IntentPolicy intent;
};

}  // namespace policy

#endif  // POLICY_POLICY_TYPES_H_
