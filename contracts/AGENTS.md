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
      "ANSWER"
    ],
    "keyword_sets": {
      "CANCEL": [
        "stop",
        "cancel"
      ],
      "ANSWER": []
    },
    "match": {
      "normalization": "lower_ascii",
      "tokenization": "semantic_words"
    }
  }
}
```
