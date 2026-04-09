#include "main/policy/contract_store.h"
#include "main/policy/soul_engine.h"

#include <cassert>

int main() {
    policy::RuntimeConfig cfg;
    cfg.voice_listening_timeout_ticks = 3;

    policy::ContractStore store;
    auto bundle = store.LoadBundle(cfg);
    policy::SoulEngine engine(bundle);

    policy::SessionContext ctx;
    ctx.clock_ticks = 3;
    ctx.voice_detected = false;

    auto out = engine.EvaluateSession(ctx);
    assert(out.emit_reprompt);
    assert(out.reset_clock);
    return 0;
}
