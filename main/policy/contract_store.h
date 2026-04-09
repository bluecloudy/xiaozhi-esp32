#ifndef POLICY_CONTRACT_STORE_H_
#define POLICY_CONTRACT_STORE_H_

#include "policy_types.h"

namespace policy {

class ContractStore {
public:
    PolicyBundle LoadBundle(const RuntimeConfig& runtime_cfg) const;
};

}  // namespace policy

#endif  // POLICY_CONTRACT_STORE_H_
