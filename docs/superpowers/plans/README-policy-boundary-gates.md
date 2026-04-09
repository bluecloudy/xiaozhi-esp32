# Policy Boundary Gates

Phase 1 gates:
- ExtensionManager is delegation-only.
- SoulEngine evaluates `PolicyBundle` data only.
- ContractStore compiles behavior data from generated contract artifacts.
- Behavior literals are banned from policy evaluator and facade files.
