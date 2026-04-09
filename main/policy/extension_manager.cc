#include "main/policy/extension_manager.h"

namespace policy {

ExtensionManager::ExtensionManager(const RuntimeConfig& runtime_cfg)
    : bundle_(ContractStore().LoadBundle(runtime_cfg)),
      engine_(bundle_) {}

SttDecision ExtensionManager::OnStt(const SttContext& ctx) const {
    return engine_.EvaluateStt(ctx);
}

SessionDecision ExtensionManager::OnSessionTick(const SessionContext& ctx) const {
    return engine_.EvaluateSession(ctx);
}

}  // namespace policy
