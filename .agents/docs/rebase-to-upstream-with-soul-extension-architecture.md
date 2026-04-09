# Rebase to Upstream with SOUL Extension Architecture

Date: 2026-04-09
Scope: Minimal migration design (no large framework rewrite)
Branch Snapshot: 17 commits ahead of main, 43 changed files, 5496 insertions / 97 deletions

## Summary

The repository already has a viable extension backbone: MCP tool registration and invocation with board-level custom tool injection. The main drift risk is not missing capability infrastructure; it is behavior/policy logic accumulated in core orchestration paths (especially Application event handling).

Recommended direction:
- Keep core close to upstream and focused on runtime primitives.
- Reuse MCP as the capability layer.
- Add a minimal ExtensionManager + SoulEngine adapter layer.
- Move product/business behavior into external contracts (SOUL/IDENTITY/USER/AGENTS/TOOLS/MEMORY), with local defaults and optional remote override.

This preserves ESP32 footprint and allows disciplined staged rebasing.

## 1) Current Addon/Plugin/Tool Management Analysis

### Existing mechanism found

1. MCP tool registry is already first-class.
- Common tools: `McpServer::AddCommonTools()` in `main/mcp_server.cc`.
- User-only tools: `McpServer::AddUserOnlyTools()` in `main/mcp_server.cc`.
- Dynamic invocation: `tools/list` and `tools/call` in `main/mcp_server.cc`.

2. Custom tool placement convention already exists.
- Core comment explicitly states custom tools should not be added to common core registration, and should be added in board `InitializeTools`.
- This is exactly the extension boundary we should preserve.

3. Board-level modularity already used widely.
- Many boards add tools in board-specific `InitializeTools` methods.
- Example pattern is documented in `docs/mcp-usage.en.md`.

4. Application already routes MCP messages through McpServer.
- Incoming protocol JSON type `mcp` delegates payload parsing to `McpServer::ParseMessage`.

5. Internal policy plugin systems were introduced for STT/TTS.
- `TextProcessingPipeline` (`main/text/text_processing_pipeline.*`).
- `OutputProcessingPipeline` (`main/text/output_processing_pipeline.*`).
- `voice_session_policy.h` for turn/session decisions.

### Conclusion of analysis

- Existing addon/plugin/tool mechanism is sufficient as the extension backbone.
- MCP + board tool registration should be reused directly.
- What is missing is a thin behavior contract adapter (SoulEngine boundary), not a new capability framework.

## 2) Architecture Conclusion

Use a facade-first staged extraction architecture:

- Keep MCP as capability and execution layer.
- Keep board `InitializeTools` as tool extension point.
- Add a minimal on-device policy facade:
  - `ExtensionManager` (wiring + contract load lifecycle)
  - `SoulEngine` (deterministic policy resolution)
- Keep `Application` as orchestrator only.
- Route behavior decisions through SoulEngine hooks rather than hardcoded branches.

This keeps upstream merge/rebase cost low by minimizing invasive core edits.

## 3) KEEP / EXTRACT / REMOVE Classification

| Category | Item | Why | Target Action |
|---|---|---|---|
| KEEP | MCP registry and dispatch (`main/mcp_server.cc`) | Already stable extension backbone | Keep as-is, build policy mapping on top |
| KEEP | Board tool injection pattern (`InitializeTools`) | Existing low-conflict extension seam | Keep and standardize for custom capabilities |
| KEEP | Audio/protocol performance hardening (`main/audio/audio_service.*`, `main/protocols/mqtt_protocol.*`) | Additive runtime robustness, not business behavior | Keep; upstream as optional perf PRs where possible |
| KEEP | Fatal error LED state handling (`main/device_state_machine.cc`, `main/led/*`) | Operational safety/observability | Keep |
| KEEP | Wake-word runtime robustness (`main/audio/wake_words/custom_wake_word.cc`) | Hardware/runtime reliability | Keep, but keep policy text out of core |
| EXTRACT | STT/TTS behavior decisions currently in `main/application.cc` | Behavior logic in core increases rebase conflicts | Move rule ownership to SOUL/AGENTS contracts via SoulEngine hooks |
| EXTRACT | Session/turn policy values in `main/voice_session_policy.h` | Good adapter shell, but behavior thresholds should be contract-driven | Keep shell; externalize rule values/strategy |
| EXTRACT | Text pipeline behavior seeds/intent mappings in `main/text/*` | Product behavior drift source | Keep execution primitives; move policy data to contracts |
| EXTRACT | assistant_name/response-mode behavior in `main/application.cc` + settings keys | Identity/policy belongs to contract layer | Resolve via IDENTITY/SOUL contracts, not app branches |
| REMOVE/REVERT | Unused demo flag (`PHASE1_INVESTOR_DEMO_BUILD`) only in Kconfig | Drift without runtime value | Remove or move to overlay branch, not upstream-facing core |
| REMOVE/REVERT | Board-specific product override that is not generally upstream-safe (battery force-disable in xingzhi board) | Potentially inappropriate global baseline behavior | Keep only in board overlay branch/profile |
| REMOVE/REVERT | Any future behavior-specific copy/if-else in `Application` | Direct source of architecture drift | Enforce policy-through-contract rule |

## 4) Minimal Hook Points to Add/Use

Only three hook families are needed.

### Hook A: STT Input Policy
- Existing insertion point: protocol JSON `type == stt` handling in `main/application.cc`.
- Purpose: decide `continue/ignore/reprompt/abort`, normalize transcript, derive intent.
- Contract form:
  - `before_send_stt(context) -> {action, transformed_text, reprompt_key, intent}`

### Hook B: TTS Output Policy
- Existing insertion point: protocol JSON `type == tts`, state `sentence_start` in `main/application.cc`.
- Purpose: suppress malformed/system output, language routing, reprompt rendering, lesson-flow shaping.
- Contract form:
  - `before_render_tts(context) -> {action, transformed_text}`

### Hook C: Session/Turn-Taking Policy
- Existing insertion points: wake event handling, listening timeout tick, TTS stop transition in `main/application.cc`.
- Purpose: wake transitions, timeout/reprompt behavior, speaking->listening/idle target.
- Contract form:
  - `on_wake_event(context) -> transition_decision`
  - `on_listening_tick(context) -> timeout_decision`
  - `on_tts_stop(context) -> target_state`

No extra generic plugin framework is required.

## 5) Proposed Minimal Extension Architecture

```text
Protocol Events (stt/tts/wake/mcp)
          |
          v
      Application (orchestrator only)
          |
          +--> SoulEngine Hook Adapter (minimal)
          |       |
          |       +--> ContractStore (local defaults + optional remote override)
          |       +--> SOUL/IDENTITY/USER/AGENTS/TOOLS/MEMORY resolvers
          |
          +--> MCP Capability Plane (existing)
                  |
                  +--> McpServer tools/list + tools/call
                  +--> Board InitializeTools custom capabilities
```

### New pieces (small)

1. `ExtensionManager`
- Responsibilities: contract load lifecycle, schema validation, hook wiring.
- No dynamic code loading, no heavy dependency graph.

2. `SoulEngine`
- Deterministic decision engine using contract-derived policy structures.
- Returns compact decision structs consumed by existing core calls.

3. `ToolPolicyAdapter`
- Maps behavior intent -> MCP tool names/guardrails from TOOLS contract.

4. `ContractStore`
- Local versioned defaults (repo source of truth).
- Optional remote override with strict validation + fallback.

## 6) Contract File Responsibilities

### SOUL.md
- Deterministic behavior/policy contract.
- Must be machine-readable enough for extraction into structured decisions.
- Owns turn-taking strategy, response shaping policy, safety behavior gates.

### IDENTITY.md
- Assistant identity/persona/voice profile defaults.
- No tool wiring or business logic.

### USER.md
- Owner/family/child profile and preference contract.
- Inputs personalization constraints used by SOUL decisions.

### AGENTS.md
- Agent orchestration and priority/conflict rules.
- Defines routing/precedence among policy modules.

### TOOLS.md
- Capability registry policy and intent-to-tool mapping.
- Allowlist/denylist, invocation guards, MCP mapping metadata.

### MEMORY.md
- Long-term memory schema and retention/access policy.
- Read/write constraints and summarization boundaries.

## 7) Anti-Hardcode Rules (Enforcement)

1. No assistant-name hardcode in core C++.
2. No user-facing text hardcode in core C++.
3. No lesson/business logic hardcode in core orchestration paths.
4. No behavior-specific if/else chains in Application when a SOUL/AGENTS decision can own it.
5. Tool usage must be configured/registered via TOOLS + MCP mapping, not embedded behavior.

Implementation guardrails:
- Add PR checklist gate: "Behavior change touches contracts first?"
- Add static review rule for new user-facing literals in `main/application.cc`.

## 8) Migration Plan (Staged, Disciplined)

### Phase 0: Baseline + Safety Tag
- Tag current branch state.
- Freeze classification and identify conflict-prone files.

### Phase 1: Seam Insertion (No Behavior Change)
- Introduce `ExtensionManager` + `SoulEngine` interfaces with pass-through behavior.
- Replace direct branch logic calls with hook invocations returning same decisions.

### Phase 2: Contract Externalization
- Move behavior rules/threshold ownership from code into contracts.
- Keep Kconfig for hardware/runtime constraints only (VAD/audio/timeouts where hardware-sensitive).

### Phase 3: Rebase Loop
- Rebase from upstream main after each phase checkpoint.
- Resolve conflicts while hook diff remains narrow and additive.

### Phase 4: Core Cleanup
- Remove policy-specific remnants from `Application`.
- Keep policy execution primitives if lightweight, but make them data-driven by contracts.

### Phase 5: Convergence and Lock
- Finalize architecture constraints in docs and PR template.
- Prevent future in-core behavior drift.

## 9) Remaining Risks

1. Contract parsing overhead on ESP32 if contract format becomes too dynamic.
- Mitigation: bounded schema, compact parse model, deterministic fallback path.

2. Dual-path behavior during migration.
- Mitigation: phase gates and temporary compatibility wrappers with explicit removal date.

3. Rebase conflicts if policy keeps landing in `Application` during migration.
- Mitigation: temporary branch protection/review rule for orchestration-only changes.

4. Drift between local and remote contract override.
- Mitigation: versioned contract metadata + safe fallback to local canonical defaults.

## 10) Final Recommended Next Steps

1. Create the minimal `ExtensionManager` and `SoulEngine` interfaces with no behavior change.
2. Define a compact machine-readable schema for SOUL/AGENTS decisions (inputs/outputs only).
3. Wire three hook families (STT input, TTS output, session transitions) to pass-through adapters first.
4. Migrate one behavior slice at a time to contracts (reprompt policy first, then session thresholds, then intent routing).
5. Start phase-based rebasing from upstream main after each extracted slice.
