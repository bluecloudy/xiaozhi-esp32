---
name: AGENTS
version: 1
schema: soul-policy-v1
mode: deterministic
---

## policy_data
```json
{
  "intent": {
    "precedence": [
      "CANCEL",
      "ANSWER",
      "COMMAND",
      "SMALL_TALK",
      "UNKNOWN"
    ],

    "keyword_sets": {
      "CANCEL": [],
      "COMMAND": [],
      "SMALL_TALK": []
    },

    "match": {
      "normalization": "lower_ascii",
      "tokenization": "semantic_words"
    }
  },

  "learning_flow_guard": {
    "enabled": true,

    "when_waiting_for_answer": {
      "allow": [
        "ANSWER"
      ],
      "cancel": [
        "CANCEL"
      ],
      "block": [
        "COMMAND",
        "SMALL_TALK",
        "UNKNOWN"
      ]
    },

    "on_block": {
      "action": "reprompt",
      "reason": "OFF_TASK",
      "reprompt_key": "TRY_AGAIN_SIMPLE"
    }
  }
}
```
