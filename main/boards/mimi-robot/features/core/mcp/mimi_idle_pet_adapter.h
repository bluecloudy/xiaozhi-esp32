#pragma once

#ifdef CONFIG_IDLE_PET_ENABLE

#include "idle_pet_adapter.h"

// Translates IdlePet mood events into MimiController servo actions.
// Falls back to leg/body actions when hand servos are not configured.
class MimiIdlePetAdapter : public IdlePetAdapter {
public:
    bool IsBusy() const override;
    void ExecuteMoodAction(PetMood mood,
                           PetPersonality personality,
                           bool media_playing) override;
    void Cancel() override;
};

#endif // CONFIG_IDLE_PET_ENABLE
