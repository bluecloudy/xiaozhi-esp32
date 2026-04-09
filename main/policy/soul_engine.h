#ifndef POLICY_SOUL_ENGINE_H_
#define POLICY_SOUL_ENGINE_H_

#include "policy_types.h"

#include <string>

namespace policy {

struct SttContext {
    std::string normalized_text;
    bool waiting_for_answer = false;
};

struct SessionContext {
    int clock_ticks = 0;
    bool voice_detected = false;
};

struct SttDecision {
    PolicyAction action = PolicyAction::kContinue;
    std::string reason;
    std::string reprompt_key;
    std::string intent;
};

struct SessionDecision {
    bool emit_reprompt = false;
    bool reset_clock = false;
    std::string reprompt_key;
    std::string reason;
};

class SoulEngine {
public:
    explicit SoulEngine(const PolicyBundle& policy) : policy_(policy) {}

    SttDecision EvaluateStt(const SttContext& ctx) const;
    SessionDecision EvaluateSession(const SessionContext& ctx) const;

private:
    const PolicyBundle& policy_;
};

}  // namespace policy

#endif  // POLICY_SOUL_ENGINE_H_
