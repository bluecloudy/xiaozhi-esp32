#ifndef VOICE_SESSION_POLICY_H_
#define VOICE_SESSION_POLICY_H_

#include "device_state.h"
#include "text/output_processing_pipeline.h"
#include "text/text_processing_pipeline.h"

namespace voice_session {

struct VoiceSessionContext {
    DeviceState state = kDeviceStateUnknown;
    bool is_manual_listening_mode = false;
    bool is_auto_listening_mode = true;
    bool play_popup_on_listening = false;
    bool audio_processor_running = false;
    bool voice_detected_in_listening = false;
    int clock_ticks = 0;
};

struct VoiceSessionConfig {
    int listening_timeout_ticks = 5;
    int tts_grace_period_ms = 400;
};

enum class WakeWordTransition {
    NONE,
    INVOKE_FROM_IDLE,
    RESTART_IN_LISTENING,
    RESTART_FROM_SPEAKING,
    EXIT_ACTIVATION_TO_IDLE,
};

struct WakeWordDecision {
    WakeWordTransition transition = WakeWordTransition::NONE;
    bool should_record_wake_latency = false;
    bool should_encode_wake_word = false;
    bool should_abort_speaking = false;
    bool should_abort_with_wake_reason = false;
    bool should_clear_send_queue = false;
    bool should_send_start_listening = false;
    bool should_reset_decoder = false;
    bool should_play_popup_immediately = false;
    bool should_set_popup_on_listening = false;
    bool should_reenable_wake_word_detection = false;
};

inline WakeWordDecision DecideWakeWordEvent(const VoiceSessionContext& context) {
    WakeWordDecision decision;
    switch (context.state) {
        case kDeviceStateIdle:
            decision.transition = WakeWordTransition::INVOKE_FROM_IDLE;
            decision.should_record_wake_latency = true;
            decision.should_encode_wake_word = true;
            break;
        case kDeviceStateListening:
            decision.transition = WakeWordTransition::RESTART_IN_LISTENING;
            decision.should_abort_speaking = true;
            decision.should_abort_with_wake_reason = true;
            decision.should_clear_send_queue = true;
            decision.should_send_start_listening = true;
            decision.should_reset_decoder = true;
            decision.should_play_popup_immediately = true;
            decision.should_reenable_wake_word_detection = true;
            break;
        case kDeviceStateSpeaking:
            decision.transition = WakeWordTransition::RESTART_FROM_SPEAKING;
            decision.should_abort_speaking = true;
            decision.should_abort_with_wake_reason = true;
            decision.should_clear_send_queue = true;
            decision.should_set_popup_on_listening = true;
            break;
        case kDeviceStateActivating:
            decision.transition = WakeWordTransition::EXIT_ACTIVATION_TO_IDLE;
            break;
        default:
            break;
    }
    return decision;
}

struct WakeInvokeDecision {
    bool should_store_wake_phrase = true;
    bool should_ignore_next_stt_after_wake = true;
    bool should_send_wake_word_detected = false;
    bool should_set_popup_on_listening = true;
};

inline WakeInvokeDecision DecideWakeInvoke(bool send_wake_word_data) {
    WakeInvokeDecision decision;
    decision.should_send_wake_word_detected = send_wake_word_data;
    decision.should_set_popup_on_listening = !send_wake_word_data;
    return decision;
}

struct ListeningEntryDecision {
    bool should_wait_for_playback_queue_empty = false;
    bool should_apply_grace_period = false;
    int grace_period_ms = 0;
    bool should_send_start_listening = false;
    bool should_enable_voice_processing = false;
    bool should_play_popup = false;
    bool should_clear_popup_flag = false;
};

inline ListeningEntryDecision DecideListeningEntry(const VoiceSessionContext& context,
                                                   const VoiceSessionConfig& config = {}) {
    ListeningEntryDecision decision;
    const bool should_start_voice_path =
        context.play_popup_on_listening || !context.audio_processor_running;

    if (!should_start_voice_path) {
        return decision;
    }

    if (context.is_auto_listening_mode) {
        decision.should_wait_for_playback_queue_empty = true;
        decision.should_apply_grace_period = true;
        decision.grace_period_ms = config.tts_grace_period_ms;
    }

    decision.should_send_start_listening = true;
    decision.should_enable_voice_processing = true;

    if (context.play_popup_on_listening) {
        decision.should_play_popup = true;
        decision.should_clear_popup_flag = true;
    }

    return decision;
}

struct ListeningTimeoutDecision {
    bool should_abort_speaking = false;
    bool should_emit_reprompt = false;
    bool should_reset_clock_ticks = false;
    const char* reprompt_text_key = text::kOutputRepromptTextKeyTryAgainSimple;
};

inline ListeningTimeoutDecision DecideListeningTimeout(const VoiceSessionContext& context,
                                                       const VoiceSessionConfig& config = {}) {
    ListeningTimeoutDecision decision;
    if (context.state != kDeviceStateListening) {
        return decision;
    }

    if (context.clock_ticks < config.listening_timeout_ticks || context.voice_detected_in_listening) {
        return decision;
    }

    decision.should_abort_speaking = true;
    decision.should_emit_reprompt = true;
    decision.should_reset_clock_ticks = true;
    return decision;
}

struct SttTurnDecision {
    bool should_abort_speaking = false;
    bool should_emit_reprompt = false;
};

inline SttTurnDecision DecideSttTurn(text::ProcessingAction action) {
    SttTurnDecision decision;
    if (action == text::ProcessingAction::ABORT) {
        decision.should_abort_speaking = true;
        return decision;
    }

    if (action == text::ProcessingAction::REPROMPT) {
        decision.should_abort_speaking = true;
        decision.should_emit_reprompt = true;
    }

    return decision;
}

enum class TtsStopTargetState {
    NONE,
    IDLE,
    LISTENING,
};

inline TtsStopTargetState DecideTtsStopTarget(const VoiceSessionContext& context) {
    if (context.state != kDeviceStateSpeaking) {
        return TtsStopTargetState::NONE;
    }

    if (context.is_manual_listening_mode) {
        return TtsStopTargetState::IDLE;
    }

    return TtsStopTargetState::LISTENING;
}

inline bool ShouldAbortWhenStartingManualListen(const VoiceSessionContext& context) {
    return context.state == kDeviceStateSpeaking;
}

}  // namespace voice_session

#endif  // VOICE_SESSION_POLICY_H_
