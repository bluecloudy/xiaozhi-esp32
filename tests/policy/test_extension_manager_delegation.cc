#include "main/policy/extension_manager.h"

#include <cassert>

int main() {
    policy::RuntimeConfig cfg;
    cfg.voice_listening_timeout_ticks = 5;

    policy::ExtensionManager mgr(cfg);

    policy::SttContext stt;
    stt.normalized_text = "";

    auto out = mgr.OnStt(stt);
    assert(out.action == policy::PolicyAction::kReprompt);
    assert(!out.reprompt_key.empty());
    return 0;
}
