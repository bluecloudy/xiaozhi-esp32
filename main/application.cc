#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "text/text_processing_pipeline.h"
#include "voice_session_policy.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <esp_heap_caps.h>
#include <font_awesome.h>
#include <algorithm>

#define TAG "Application"

namespace {

constexpr bool IsTextNormalizationEnabled() {
#ifdef CONFIG_ENABLE_TEXT_NORMALIZATION
    return true;
#else
    return false;
#endif
}

constexpr bool IsWakeWordEchoFilterEnabled() {
#ifdef CONFIG_ENABLE_WAKEWORD_ECHO_FILTER
    return true;
#else
    return false;
#endif
}

constexpr bool IsKidAnswerValidationFeatureEnabled() {
#ifdef CONFIG_ENABLE_KID_ANSWER_VALIDATION
    return true;
#else
    return false;
#endif
}

constexpr bool IsRepromptPolicyEnabled() {
#ifdef CONFIG_ENABLE_REPROMPT_POLICY
    return true;
#else
    return false;
#endif
}

constexpr bool IsPhoneticNormalizationEnabled() {
#ifdef CONFIG_ENABLE_PHONETIC_NORMALIZATION
    return true;
#else
    return false;
#endif
}

constexpr bool IsBilingualRoutingEnabled() {
#ifdef CONFIG_ENABLE_BILINGUAL_ROUTING
    return true;
#else
    return false;
#endif
}

constexpr bool IsTtsOutputPolicyEnabled() {
#ifdef CONFIG_ENABLE_TTS_OUTPUT_POLICY
    return true;
#else
    return false;
#endif
}

constexpr bool IsLearningFlowOutputGuardEnabled() {
#ifdef CONFIG_ENABLE_LEARNING_FLOW_OUTPUT_GUARD
    return true;
#else
    return false;
#endif
}

voice_session::VoiceSessionConfig GetVoiceSessionConfig() {
    voice_session::VoiceSessionConfig config;
    config.listening_timeout_ticks = CONFIG_VOICE_SESSION_LISTENING_TIMEOUT_TICKS;
    config.tts_grace_period_ms = CONFIG_TTS_GRACE_PERIOD_MS;
    return config;
}

std::string ToUpperAscii(std::string value) {
    for (char& ch : value) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 0x80) {
            ch = static_cast<char>(std::toupper(uch));
        }
    }
    return value;
}

text::ExpectedLanguageMode ParseExpectedLanguageMode(const std::string& value,
                                                     text::ExpectedLanguageMode fallback) {
    std::string normalized = ToUpperAscii(value);
    if (normalized == "VI_ONLY" || normalized == "VI") {
        return text::ExpectedLanguageMode::VI_ONLY;
    }
    if (normalized == "EN_ONLY" || normalized == "EN") {
        return text::ExpectedLanguageMode::EN_ONLY;
    }
    if (normalized == "BILINGUAL" || normalized == "VI_EN" || normalized == "EN_VI") {
        return text::ExpectedLanguageMode::BILINGUAL;
    }
    return fallback;
}

text::ExpectedLanguageMode ResolveExpectedLanguageModeFromJson(
    const cJSON* root,
    text::ExpectedLanguageMode fallback) {
    auto expected_language_mode = cJSON_GetObjectItem(root, "expected_language_mode");
    if (!cJSON_IsString(expected_language_mode)) {
        expected_language_mode = cJSON_GetObjectItem(root, "language_mode");
    }

    if (cJSON_IsString(expected_language_mode)) {
        return ParseExpectedLanguageMode(expected_language_mode->valuestring, fallback);
    }

    return fallback;
}

text::ResponseType ResolveResponseTypeFromJson(const cJSON* root,
                                               bool is_learning_mode,
                                               bool is_story_mode) {
    auto response_type = cJSON_GetObjectItem(root, "response_type");
    if (cJSON_IsString(response_type)) {
        std::string normalized = ToUpperAscii(response_type->valuestring);
        if (normalized == "CHAT") {
            return text::ResponseType::CHAT;
        }
        if (normalized == "LESSON") {
            return text::ResponseType::LESSON;
        }
        if (normalized == "REPROMPT") {
            return text::ResponseType::REPROMPT;
        }
        if (normalized == "STORY") {
            return text::ResponseType::STORY;
        }
        if (normalized == "MEDIA_CONFIRMATION") {
            return text::ResponseType::MEDIA_CONFIRMATION;
        }
    }

    if (is_story_mode) {
        return text::ResponseType::STORY;
    }
    if (is_learning_mode) {
        return text::ResponseType::LESSON;
    }
    return text::ResponseType::CHAT;
}

} // namespace

static bool s_voice_detected_in_listening = false;

Application::Application() {
    event_group_ = xEventGroupCreate();

    text::TextNormalizationOptions normalization_options = {
        .enable_text_normalization = IsTextNormalizationEnabled(),
        .enable_phonetic_normalization = IsPhoneticNormalizationEnabled(),
    };

    text::TextProcessingPipelineConfig pipeline_config;
    pipeline_config.normalization_options = normalization_options;
    pipeline_config.enable_wake_word_echo_filter = IsWakeWordEchoFilterEnabled();
    pipeline_config.enable_kid_answer_validation = IsKidAnswerValidationFeatureEnabled();
    pipeline_config.enable_reprompt_policy = IsRepromptPolicyEnabled();
    stt_processing_pipeline_ = std::make_unique<text::TextProcessingPipeline>(pipeline_config);

    text::OutputProcessingPipelineConfig output_pipeline_config;
    output_pipeline_config.enable_output_normalization = IsTextNormalizationEnabled();
    output_pipeline_config.enable_bilingual_routing = IsBilingualRoutingEnabled();
    output_pipeline_config.enable_tts_output_policy = IsTtsOutputPolicyEnabled();
    output_pipeline_config.enable_learning_flow_guard = IsLearningFlowOutputGuardEnabled();
    output_pipeline_config.enable_reprompt_output = IsRepromptPolicyEnabled();
    output_processing_pipeline_ = std::make_unique<text::OutputProcessingPipeline>(output_pipeline_config);

#if defined(CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK) && CONFIG_ENABLE_STT_PIPELINE_SELF_CHECK
    if (!text::RunTextPipelineSelfCheck()) {
        ESP_LOGW(TAG, "STT text pipeline self-check failed");
    }
    if (!text::RunOutputPipelineSelfCheck()) {
        ESP_LOGW(TAG, "Output text pipeline self-check failed");
    }
#endif

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::RecordVoiceLatencyTimestamp(const char* stage, int64_t timestamp_us) {
    if (voice_latency_trace_.logged) {
        voice_latency_trace_ = VoiceLatencyTrace{};
    }

    if (strcmp(stage, "wake_detected") == 0) {
        voice_latency_trace_ = VoiceLatencyTrace{};
        voice_latency_trace_.wake_detected_us = timestamp_us;
    } else if (strcmp(stage, "speech_start") == 0) {
        if (voice_latency_trace_.wake_detected_us < 0 || voice_latency_trace_.speech_start_us >= 0) {
            return;
        }
        voice_latency_trace_.speech_start_us = timestamp_us;
    } else if (strcmp(stage, "speech_end") == 0) {
        if (voice_latency_trace_.speech_start_us < 0 || voice_latency_trace_.speech_end_us >= 0) {
            return;
        }
        voice_latency_trace_.speech_end_us = timestamp_us;
    } else if (strcmp(stage, "request_sent") == 0) {
        if (voice_latency_trace_.speech_end_us < 0 || voice_latency_trace_.request_sent_us >= 0) {
            return;
        }
        voice_latency_trace_.request_sent_us = timestamp_us;
    } else if (strcmp(stage, "response_received") == 0) {
        if (voice_latency_trace_.request_sent_us < 0 || voice_latency_trace_.response_received_us >= 0) {
            return;
        }
        voice_latency_trace_.response_received_us = timestamp_us;
    } else if (strcmp(stage, "playback_start") == 0) {
        if (voice_latency_trace_.response_received_us < 0 || voice_latency_trace_.playback_start_us >= 0) {
            return;
        }
        voice_latency_trace_.playback_start_us = timestamp_us;
    } else {
        return;
    }

    auto ts_us_str = std::to_string(timestamp_us);
    ESP_LOGI(TAG, "[latency] stage=%s ts_us=%s", stage, ts_us_str.c_str());
    TryLogVoiceLatency();
}

void Application::TryLogVoiceLatency() {
    if (voice_latency_trace_.logged ||
        voice_latency_trace_.wake_detected_us < 0 ||
        voice_latency_trace_.speech_start_us < 0 ||
        voice_latency_trace_.speech_end_us < 0 ||
        voice_latency_trace_.request_sent_us < 0 ||
        voice_latency_trace_.response_received_us < 0 ||
        voice_latency_trace_.playback_start_us < 0) {
        return;
    }

    int64_t wake_to_speech_start_ms = std::max<int64_t>(0, (voice_latency_trace_.speech_start_us - voice_latency_trace_.wake_detected_us) / 1000);
    int64_t uplink_ms = std::max<int64_t>(0, (voice_latency_trace_.request_sent_us - voice_latency_trace_.speech_end_us) / 1000);
    int64_t server_ms = std::max<int64_t>(0, (voice_latency_trace_.response_received_us - voice_latency_trace_.request_sent_us) / 1000);
    int64_t playback_queue_ms = std::max<int64_t>(0, (voice_latency_trace_.playback_start_us - voice_latency_trace_.response_received_us) / 1000);
    int64_t e2e_ms = std::max<int64_t>(0, (voice_latency_trace_.playback_start_us - voice_latency_trace_.wake_detected_us) / 1000);

    auto wake_detected_us_str = std::to_string(voice_latency_trace_.wake_detected_us);
    auto speech_start_us_str = std::to_string(voice_latency_trace_.speech_start_us);
    auto speech_end_us_str = std::to_string(voice_latency_trace_.speech_end_us);
    auto request_sent_us_str = std::to_string(voice_latency_trace_.request_sent_us);
    auto response_received_us_str = std::to_string(voice_latency_trace_.response_received_us);
    auto playback_start_us_str = std::to_string(voice_latency_trace_.playback_start_us);
    auto wake_to_speech_start_ms_str = std::to_string(wake_to_speech_start_ms);
    auto uplink_ms_str = std::to_string(uplink_ms);
    auto server_ms_str = std::to_string(server_ms);
    auto playback_queue_ms_str = std::to_string(playback_queue_ms);
    auto e2e_ms_str = std::to_string(e2e_ms);

    ESP_LOGI(TAG,
             "[latency] wake_detected_us=%s speech_start_us=%s speech_end_us=%s request_sent_us=%s response_received_us=%s playback_start_us=%s wake_to_speech_start_ms=%s uplink_ms=%s server_ms=%s playback_queue_ms=%s e2e_ms=%s",
             wake_detected_us_str.c_str(),
             speech_start_us_str.c_str(),
             speech_end_us_str.c_str(),
             request_sent_us_str.c_str(),
             response_received_us_str.c_str(),
             playback_start_us_str.c_str(),
             wake_to_speech_start_ms_str.c_str(),
             uplink_ms_str.c_str(),
             server_ms_str.c_str(),
             playback_queue_ms_str.c_str(),
             e2e_ms_str.c_str());
    voice_latency_trace_.logged = true;
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        ESP_LOGI(TAG, "Wake trigger source=voice callback, phrase=%s", wake_word.c_str());
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        if (speaking) {
            RecordVoiceLatencyTimestamp("speech_start", esp_timer_get_time());
        } else {
            RecordVoiceLatencyTimestamp("speech_end", esp_timer_get_time());
        }
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    callbacks.on_playback_started = [this]() {
        RecordVoiceLatencyTimestamp("playback_start", esp_timer_get_time());
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateFatalError);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_) {
                    if (voice_latency_trace_.request_sent_us < 0) {
                        RecordVoiceLatencyTimestamp("request_sent", esp_timer_get_time());
                    }
                    if (!protocol_->SendAudio(std::move(packet))) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
                if (audio_service_.IsVoiceDetected()) {
                    s_voice_detected_in_listening = true;
                }
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            voice_session::VoiceSessionContext session_context;
            session_context.state = GetDeviceState();
            session_context.clock_ticks = clock_ticks_;
            session_context.voice_detected_in_listening = s_voice_detected_in_listening;
            const auto voice_session_config = GetVoiceSessionConfig();
            auto timeout_decision = voice_session::DecideListeningTimeout(session_context, voice_session_config);
            if (timeout_decision.should_emit_reprompt) {
                ESP_LOGW(TAG, "[asr] listening window timeout");
                if (protocol_ && timeout_decision.should_abort_speaking) {
                    protocol_->SendAbortSpeaking(kAbortReasonNone);
                }

                text::OutputContext output_context;
                output_context.is_learning_mode = kid_answer_validation_enabled_;
                output_context.is_waiting_for_answer = is_waiting_for_answer_;
                output_context.is_parent_mode = parent_mode_enabled_;
                output_context.is_story_mode = story_mode_enabled_;
                output_context.expected_language_mode = expected_language_mode_;
                output_context.response_type = text::ResponseType::REPROMPT;
                output_context.reprompt_text_key = timeout_decision.reprompt_text_key;
                output_context.assistant_name = assistant_name_;

                auto output_result = output_processing_pipeline_->Process(output_context);
                if (output_result.action == text::OutputAction::CONTINUE &&
                    !output_context.normalized_text.empty()) {
                    if (output_context.is_learning_mode) {
                        is_waiting_for_answer_ = true;
                    }
                    std::string reprompt = output_context.normalized_text;
                    Schedule([this, reprompt = std::move(reprompt)]() {
                        if (GetDeviceState() == kDeviceStateListening) {
                            auto display = Board::GetInstance().GetDisplay();
                            display->SetChatMessage("system", reprompt.c_str());
                            // Restart listening state to maintain turn
                            SetDeviceState(kDeviceStateListening);
                        }
                    });
                }
                if (timeout_decision.should_reset_clock_ticks) {
                    clock_ticks_ = 0;
                }
            }

            // Print debug info every 10 seconds
                SystemInfo::PrintHeapStats();
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    });
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();
    Settings system_settings("system", true);
    kid_answer_validation_enabled_ = IsKidAnswerValidationFeatureEnabled() &&
                                     system_settings.GetBool("kid_answer_validation_mode", false);
    parent_mode_enabled_ = system_settings.GetBool("parent_mode", false);
    story_mode_enabled_ = system_settings.GetBool("story_mode", false);
    assistant_name_ = system_settings.GetString("assistant_name", Lang::Strings::ASSISTANT_NAME);
    expected_language_mode_ = ParseExpectedLanguageMode(
        system_settings.GetString("expected_language_mode", "BILINGUAL"),
        text::ExpectedLanguageMode::BILINGUAL);

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            if (voice_latency_trace_.response_received_us < 0) {
                RecordVoiceLatencyTimestamp("response_received", esp_timer_get_time());
            }
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    voice_session::VoiceSessionContext session_context;
                    session_context.state = GetDeviceState();
                    session_context.is_manual_listening_mode =
                        (listening_mode_ == kListeningModeManualStop);
                    auto stop_target = voice_session::DecideTtsStopTarget(session_context);
                    if (stop_target == voice_session::TtsStopTargetState::IDLE) {
                        SetDeviceState(kDeviceStateIdle);
                    } else if (stop_target == voice_session::TtsStopTargetState::LISTENING) {
                        SetDeviceState(kDeviceStateListening);
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    text::OutputContext output_context;
                    output_context.raw_text = text->valuestring;
                    output_context.normalized_text = output_context.raw_text;
                    output_context.is_learning_mode = kid_answer_validation_enabled_;
                    output_context.is_waiting_for_answer = is_waiting_for_answer_;
                    output_context.is_parent_mode = parent_mode_enabled_;
                    output_context.is_story_mode = story_mode_enabled_;
                    output_context.expected_language_mode = ResolveExpectedLanguageModeFromJson(
                        root,
                        expected_language_mode_);
                    output_context.response_type = ResolveResponseTypeFromJson(
                        root,
                        output_context.is_learning_mode,
                        output_context.is_story_mode);

                    auto output_result = output_processing_pipeline_->Process(output_context);
                    if (output_result.action == text::OutputAction::SUPPRESS) {
                        ESP_LOGW(TAG, "[tts] suppressed sentence due to output policy reason=%d",
                                 static_cast<int>(output_result.reason));
                        return;
                    }

                    if (output_context.normalized_text.empty()) {
                        ESP_LOGW(TAG, "[tts] suppressed empty sentence after output policy");
                        return;
                    }

                    if (output_context.is_learning_mode &&
                        output_context.response_type == text::ResponseType::LESSON) {
                        is_waiting_for_answer_ = true;
                    }

                    std::string message = output_context.normalized_text;
                    ESP_LOGI(TAG, "<< %s", message.c_str());
                    Schedule([display, message = std::move(message)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                std::string message = text->valuestring;
                ESP_LOGI(TAG, ">> %s", message.c_str());

                text::ProcessingContext processing_context;
                processing_context.raw_text = message;
                processing_context.normalized_text = message;
                processing_context.wake_word = last_wake_word_phrase_;
                processing_context.ignore_next_stt_after_wake = ignore_next_stt_after_wake_;
                processing_context.is_learning_mode = kid_answer_validation_enabled_;
                processing_context.is_waiting_for_answer = is_waiting_for_answer_;
                processing_context.answer_policy.expected_answer_type = kid_answer_validation_enabled_
                    ? text::ExpectedAnswerType::SHORT_PHRASE
                    : text::ExpectedAnswerType::FREE_TEXT;

                auto processing_result = stt_processing_pipeline_->Process(processing_context);
                ignore_next_stt_after_wake_ = processing_context.ignore_next_stt_after_wake;

                if (processing_result.action == text::ProcessingAction::IGNORE) {
                    ESP_LOGI(TAG, "[stt] ignored transcript='%s' wake='%s'",
                             message.c_str(), last_wake_word_phrase_.c_str());
                    return;
                }

                auto stt_turn_decision = voice_session::DecideSttTurn(processing_result.action);
                if (stt_turn_decision.should_abort_speaking) {
                    protocol_->SendAbortSpeaking(kAbortReasonNone);
                }

                if (processing_result.action == text::ProcessingAction::ABORT) {
                    if (processing_context.user_intent == text::UserIntent::CANCEL) {
                        is_waiting_for_answer_ = false;
                    }
                    return;
                }

                if (stt_turn_decision.should_emit_reprompt) {
                    ESP_LOGW(TAG, "[asr] rejected input raw='%s'", message.c_str());

                    text::OutputContext output_context;
                    output_context.is_learning_mode = kid_answer_validation_enabled_;
                    output_context.is_waiting_for_answer = is_waiting_for_answer_;
                    output_context.is_parent_mode = parent_mode_enabled_;
                    output_context.is_story_mode = story_mode_enabled_;
                    output_context.expected_language_mode = expected_language_mode_;
                    output_context.response_type = text::ResponseType::REPROMPT;
                    output_context.reprompt_text_key = processing_result.reprompt_text_key;
                    output_context.assistant_name = assistant_name_;

                    auto output_result = output_processing_pipeline_->Process(output_context);
                    if (output_result.action == text::OutputAction::CONTINUE &&
                        !output_context.normalized_text.empty()) {
                        if (output_context.is_learning_mode) {
                            is_waiting_for_answer_ = true;
                        }
                        std::string reprompt = output_context.normalized_text;
                        Schedule([this, display, reprompt = std::move(reprompt)]() {
                            if (GetDeviceState() == kDeviceStateListening && !reprompt.empty()) {
                                display->SetChatMessage("system", reprompt.c_str());
                                SetDeviceState(kDeviceStateListening);
                            }
                        });
                    }
                    return;
                }

                if (processing_context.user_intent == text::UserIntent::ANSWER) {
                    is_waiting_for_answer_ = false;
                }

                std::string display_message = processing_context.normalized_text;

                Schedule([display, message = std::move(display_message)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    ESP_LOGI(TAG, "Wake trigger source=button");
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    voice_session::VoiceSessionContext session_context;
    session_context.state = state;
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (voice_session::ShouldAbortWhenStartingManualListen(session_context)) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    voice_session::VoiceSessionContext session_context;
    session_context.state = state;
    auto wake_decision = voice_session::DecideWakeWordEvent(session_context);

    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake trigger source=voice event, phrase=%s, state=%d", wake_word.c_str(), (int)state);

    if (wake_decision.should_abort_speaking) {
        AbortSpeaking(wake_decision.should_abort_with_wake_reason
                          ? kAbortReasonWakeWordDetected
                          : kAbortReasonNone);
    }

    if (wake_decision.should_clear_send_queue) {
        while (audio_service_.PopPacketFromSendQueue());
    }

    if (wake_decision.transition == voice_session::WakeWordTransition::INVOKE_FROM_IDLE) {
        if (wake_decision.should_record_wake_latency) {
            RecordVoiceLatencyTimestamp("wake_detected", esp_timer_get_time());
        }
        if (wake_decision.should_encode_wake_word) {
            audio_service_.EncodeWakeWord();
        }
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (wake_decision.transition == voice_session::WakeWordTransition::RESTART_IN_LISTENING) {
        if (wake_decision.should_send_start_listening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
        }
        if (wake_decision.should_reset_decoder) {
            audio_service_.ResetDecoder();
        }
        if (wake_decision.should_play_popup_immediately) {
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
        }
        if (wake_decision.should_reenable_wake_word_detection) {
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        }
    } else if (wake_decision.transition == voice_session::WakeWordTransition::RESTART_FROM_SPEAKING) {
        if (wake_decision.should_set_popup_on_listening) {
            play_popup_on_listening_ = true;
        }
        SetListeningMode(GetDefaultListeningMode());
    } else if (wake_decision.transition == voice_session::WakeWordTransition::EXIT_ACTIVATION_TO_IDLE) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            audio_service_.EnableWakeWordDetection(true);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
    constexpr bool kSendWakeWordData =
#if CONFIG_SEND_WAKE_WORD_DATA
        true;
#else
        false;
#endif
    auto wake_invoke_decision = voice_session::DecideWakeInvoke(kSendWakeWordData);
    if (wake_invoke_decision.should_store_wake_phrase) {
        last_wake_word_phrase_ = wake_word;
    }
    if (wake_invoke_decision.should_ignore_next_stt_after_wake) {
        ignore_next_stt_after_wake_ = true;
    }
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    if (wake_invoke_decision.should_send_wake_word_detected) {
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
    }
    SetListeningMode(GetDefaultListeningMode());
#else
    if (wake_invoke_decision.should_set_popup_on_listening) {
        // Set flag to play popup sound after state changes to listening
        // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
        play_popup_on_listening_ = true;
    }
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            is_waiting_for_answer_ = false;
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            s_voice_detected_in_listening = false;

            {
                voice_session::VoiceSessionContext session_context;
                session_context.state = kDeviceStateListening;
                session_context.is_auto_listening_mode = (listening_mode_ == kListeningModeAutoStop);
                session_context.play_popup_on_listening = play_popup_on_listening_;
                session_context.audio_processor_running = audio_service_.IsAudioProcessorRunning();
                const auto voice_session_config = GetVoiceSessionConfig();
                auto listening_decision = voice_session::DecideListeningEntry(session_context, voice_session_config);

                if (listening_decision.should_wait_for_playback_queue_empty) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }

                if (listening_decision.should_apply_grace_period && listening_decision.grace_period_ms > 0) {
                    // Grace period after TTS to prevent echo.
                    vTaskDelay(pdMS_TO_TICKS(listening_decision.grace_period_ms));
                }

                if (listening_decision.should_send_start_listening) {
                    protocol_->SendStartListening(listening_mode_);
                }

                if (listening_decision.should_enable_voice_processing) {
                    audio_service_.EnableVoiceProcessing(true);
                }

                if (listening_decision.should_clear_popup_flag) {
                    play_popup_on_listening_ = false;
                }

                if (listening_decision.should_play_popup) {
                    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
                }
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", Lang::Strings::UPGRADE_SUCCESS);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}

