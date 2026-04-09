#ifndef POLICY_EXTENSION_MANAGER_H_
#define POLICY_EXTENSION_MANAGER_H_

#include "contract_store.h"
#include "soul_engine.h"

namespace policy {

class ExtensionManager {
public:
    explicit ExtensionManager(const RuntimeConfig& runtime_cfg);

    SttDecision OnStt(const SttContext& ctx) const;
    SessionDecision OnSessionTick(const SessionContext& ctx) const;

private:
    PolicyBundle bundle_;
    SoulEngine engine_;
};

}  // namespace policy

#endif  // POLICY_EXTENSION_MANAGER_H_
