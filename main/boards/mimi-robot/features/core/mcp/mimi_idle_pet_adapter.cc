#ifdef CONFIG_IDLE_PET_ENABLE

#include "mimi_idle_pet_adapter.h"
#include "../controller/mimi_controller.h"
#include "application.h"
#include "device_state.h"

#include <cstring>
#include <esp_log.h>
#include <esp_random.h>

#define TAG "MimiIdlePet"

// Default movement params for idle pet actions — gentle, short.
static constexpr int kSteps     = 2;
static constexpr int kSpeed     = 700;
static constexpr int kDirection = 1;
static constexpr int kAmount    = 30;
static constexpr int kArmSwing  = 50;

// ─── IdlePetAdapter interface ────────────────────────────────────────────────

bool MimiIdlePetAdapter::IsBusy() const {
    MimiController* ctrl = GetMimiController();
    if (!ctrl) return false;
    return std::strcmp(MimiControllerGetActionStatus(ctrl), "idle") != 0;
}

void MimiIdlePetAdapter::Cancel() {
    MimiController* ctrl = GetMimiController();
    if (ctrl) MimiControllerStopActions(ctrl);
}

void MimiIdlePetAdapter::ExecuteMoodAction(PetMood mood,
                                            PetPersonality personality,
                                            bool media_playing) {
    MimiController* ctrl = GetMimiController();
    if (!ctrl) return;

    // Personality-based policy: some personalities skip when media is active.
    DeviceState state = Application::GetInstance().GetDeviceState();
    bool is_listening_or_speaking = (state == kDeviceStateListening ||
                                     state == kDeviceStateSpeaking);
    if (is_listening_or_speaking) return;

    switch (personality) {
        case PetPersonality::kCalm:
        case PetPersonality::kSleepy:
            // Stay still during media playback.
            if (media_playing) return;
            break;
        case PetPersonality::kBalanced:
        case PetPersonality::kCurious:
            // Only act when quiet.
            if (media_playing) return;
            break;
        case PetPersonality::kPlayful:
        case PetPersonality::kNeedy:
            // Allow light actions even during media, but guard is above.
            break;
    }

    bool has_hands = MimiControllerHasHands(ctrl);

    // Resolve action name based on mood, with hand fallback.
    const char* action = nullptr;
    switch (mood) {
        case PetMood::kCurious:
            action = has_hands ? "hand_wave" : "swing";
            break;
        case PetMood::kHappy: {
            // Random between swing / updown
            action = (esp_random() % 2 == 0) ? "swing" : "updown";
            break;
        }
        case PetMood::kPlayful: {
            // Random between updown / jump
            action = (esp_random() % 2 == 0) ? "updown" : "jump";
            break;
        }
        case PetMood::kNeedy:
            action = has_hands ? "greeting" : "bend";
            break;
        case PetMood::kSleepy:
            action = "sit";
            break;
        case PetMood::kBored: {
            // Random between shake_leg / moonwalk
            action = (esp_random() % 2 == 0) ? "shake_leg" : "moonwalk";
            break;
        }
        default:
            action = "swing";
            break;
    }

    std::string error;
    if (!MimiControllerExecuteAction(ctrl,
                                     action,
                                     kSteps,
                                     kSpeed,
                                     kDirection,
                                     kAmount,
                                     kArmSwing,
                                     &error)) {
        ESP_LOGW(TAG, "Action '%s' failed: %s", action, error.c_str());
    } else {
        ESP_LOGD(TAG, "Executing idle pet action: %s", action);
    }
}

#undef TAG

#endif // CONFIG_IDLE_PET_ENABLE
