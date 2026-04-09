#include "main/policy/contract_store.h"

#include <cassert>

int main() {
    policy::RuntimeConfig cfg;
    cfg.voice_listening_timeout_ticks = 7;

    policy::ContractStore store;
    auto bundle = store.LoadBundle(cfg);

    assert(bundle.session.listening_timeout_ticks == 7);
    assert(!bundle.intent.precedence.empty());
    return 0;
}
