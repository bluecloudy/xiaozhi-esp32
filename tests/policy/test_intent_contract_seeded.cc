#include "main/policy/contract_store.h"
#include "main/policy/soul_engine.h"

#include <cassert>

int main() {
    policy::RuntimeConfig cfg;

    policy::ContractStore store;
    auto bundle = store.LoadBundle(cfg);
    policy::SoulEngine engine(bundle);

    policy::SttContext stt;
    stt.normalized_text = "cancel";

    auto out = engine.EvaluateStt(stt);
    assert(out.intent == "CANCEL");
    return 0;
}
