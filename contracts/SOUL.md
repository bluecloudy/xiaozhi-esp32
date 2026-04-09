---
name: SOUL
version: 1
schema: soul-policy-v1
mode: deterministic
---

## policy_data
```json
{
  "stt": {
    "evaluation_order": [
      "empty_input",
      "intent_routing"
    ],
    "rules": {
      "empty_input": {
        "when": {
          "field": "stt.normalized_text",
          "op": "is_empty"
        },
        "decision": {
          "action": "reprompt",
          "reason": "NO_INPUT",
          "reprompt_key": "TRY_AGAIN_SIMPLE"
        }
      }
    }
  },
  "session": {
    "listening_timeout": {
      "source": "config_handoff",
      "config_key": "voice.listening_timeout_ticks"
    },
    "timeout_decision": {
      "action": "reprompt",
      "reason": "LISTEN_TIMEOUT",
      "reprompt_key": "TRY_AGAIN_SIMPLE",
      "reset_clock": true
    }
  }
}
```
