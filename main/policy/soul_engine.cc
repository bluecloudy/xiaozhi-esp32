#include "main/policy/soul_engine.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace policy {
namespace {

std::string LowerAscii(const std::string& input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::vector<std::string> TokenizeWords(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            normalized.push_back(static_cast<char>(std::tolower(c)));
        } else {
            normalized.push_back(' ');
        }
    }

    std::vector<std::string> tokens;
    std::stringstream ss(normalized);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool HasKeyword(const std::vector<std::string>& words, const std::vector<std::string>& keywords) {
    for (const auto& keyword : keywords) {
        const std::string normalized_keyword = LowerAscii(keyword);
        if (std::find(words.begin(), words.end(), normalized_keyword) != words.end()) {
            return true;
        }
    }
    return false;
}

}  // namespace

SttDecision SoulEngine::EvaluateStt(const SttContext& ctx) const {
    SttDecision out;
    const auto words = TokenizeWords(ctx.normalized_text);

    for (const auto& rule_type : policy_.stt.evaluation_order) {
        if (rule_type == "empty_input") {
            if (words.empty()) {
                out.action = policy_.stt.empty_input_decision.action;
                out.reason = policy_.stt.empty_input_decision.reason;
                out.reprompt_key = policy_.stt.empty_input_decision.reprompt_key;
                return out;
            }
            continue;
        }

        if (rule_type == "intent_routing") {
            for (const auto& intent_name : policy_.intent.precedence) {
                const auto set_it = policy_.intent.keyword_sets.find(intent_name);
                if (set_it == policy_.intent.keyword_sets.end()) {
                    continue;
                }
                if (HasKeyword(words, set_it->second)) {
                    out.intent = intent_name;
                    break;
                }
            }
        }
    }

    return out;
}

SessionDecision SoulEngine::EvaluateSession(const SessionContext& ctx) const {
    SessionDecision out;

    if (ctx.voice_detected) {
        return out;
    }

    if (ctx.clock_ticks < policy_.session.listening_timeout_ticks) {
        return out;
    }

    if (policy_.session.timeout_action == PolicyAction::kReprompt) {
        out.emit_reprompt = true;
        out.reprompt_key = policy_.session.timeout_reprompt_key;
        out.reason = policy_.session.timeout_reason;
        out.reset_clock = policy_.session.reset_clock;
    }

    return out;
}

}  // namespace policy
