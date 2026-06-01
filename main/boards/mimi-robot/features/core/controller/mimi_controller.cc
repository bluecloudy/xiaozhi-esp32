#include <esp_log.h>
#include <cJSON.h>

#include <cstdlib>
#include <cstring>

#include "../power/power_manager.h"
#include "mimi_controller.h"
#include "../servo/mimi_movements.h"
#include "../servo/oscillator.h"
#include "application.h"
#include "board.h"
#include "sdkconfig.h"
#include "settings.h"

#include "../../shared/common.h"

#define TAG "MimiController"

class MimiController {
private:
    Mimi mimi_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool has_hands_ = false;
    bool is_action_in_progress_ = false;

    struct MimiActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
        // Heap-allocated JSON string (only set for ACTION_SERVO_SEQUENCE, nullptr otherwise).
        // The receiver (ActionTask) is responsible for free()-ing this pointer.
        char* servo_sequence_json;
    };

    enum ActionType {
        ACTION_WALK = 1,
        ACTION_TURN = 2,
        ACTION_JUMP = 3,
        ACTION_SWING = 4,
        ACTION_MOONWALK = 5,
        ACTION_BEND = 6,
        ACTION_SHAKE_LEG = 7,
        ACTION_SIT = 25,
        ACTION_RADIO_CALISTHENICS = 26,
        ACTION_MAGIC_CIRCLE = 27,
        ACTION_UPDOWN = 8,
        ACTION_TIPTOE_SWING = 9,
        ACTION_JITTER = 10,
        ACTION_ASCENDING_TURN = 11,
        ACTION_CRUSAITO = 12,
        ACTION_FLAPPING = 13,
        ACTION_HANDS_UP = 14,
        ACTION_HANDS_DOWN = 15,
        ACTION_HAND_WAVE = 16,
        ACTION_WINDMILL = 20,
        ACTION_TAKEOFF = 21,
        ACTION_FITNESS = 22,
        ACTION_GREETING = 23,
        ACTION_SHY = 24,
        ACTION_SHOWCASE = 28,
        ACTION_HOME = 17,
        ACTION_SERVO_SEQUENCE = 18,
        ACTION_WHIRLWIND_LEG = 19
    };

    static void ActionTask(void* arg) {
        MimiController* controller = static_cast<MimiController*>(arg);
        MimiActionParams params;
        controller->mimi_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "Executing action: %d", params.action_type);
                PowerManager::PauseBatteryUpdate();
                controller->is_action_in_progress_ = true;
                if (params.action_type == ACTION_SERVO_SEQUENCE) {
                    cJSON* json = params.servo_sequence_json ? cJSON_Parse(params.servo_sequence_json) : nullptr;
                    free(params.servo_sequence_json);  // Always free after parsing
                    params.servo_sequence_json = nullptr;
                    if (json != nullptr) {
                        cJSON* actions = cJSON_GetObjectItem(json, "a");
                        if (cJSON_IsArray(actions)) {
                            int array_size = cJSON_GetArraySize(actions);
                            ESP_LOGI(TAG, "Executing servo sequence with %d actions", array_size);

                            int sequence_delay = 0;
                            cJSON* delay_item = cJSON_GetObjectItem(json, "d");
                            if (cJSON_IsNumber(delay_item)) {
                                sequence_delay = delay_item->valueint;
                                if (sequence_delay < 0)
                                    sequence_delay = 0;
                            }

                            int current_positions[SERVO_COUNT];
                            for (int j = 0; j < SERVO_COUNT; j++) {
                                current_positions[j] = 90;
                            }
                            current_positions[LEFT_HAND] = 45;
                            current_positions[RIGHT_HAND] = 180 - 45;

                            for (int i = 0; i < array_size; i++) {
                                cJSON* action_item = cJSON_GetArrayItem(actions, i);
                                if (cJSON_IsObject(action_item)) {
                                    cJSON* osc_item = cJSON_GetObjectItem(action_item, "osc");
                                    if (cJSON_IsObject(osc_item)) {
                                        int amplitude[SERVO_COUNT] = {0};
                                        int center_angle[SERVO_COUNT] = {0};
                                        double phase_diff[SERVO_COUNT] = {0};
                                        int period = 300;
                                        float steps = 8.0;

                                        const char* servo_names[] = {"ll", "rl", "lf",
                                                                     "rf", "lh", "rh"};

                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            amplitude[j] = 0;
                                        }
                                        cJSON* amp_item = cJSON_GetObjectItem(osc_item, "a");
                                        if (cJSON_IsObject(amp_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* amp_value =
                                                    cJSON_GetObjectItem(amp_item, servo_names[j]);
                                                if (cJSON_IsNumber(amp_value)) {
                                                    int amp = amp_value->valueint;
                                                    if (amp >= 10 && amp <= 90) {
                                                        amplitude[j] = amp;
                                                    }
                                                }
                                            }
                                        }

                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            center_angle[j] = 90;
                                        }
                                        cJSON* center_item = cJSON_GetObjectItem(osc_item, "o");
                                        if (cJSON_IsObject(center_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* center_value = cJSON_GetObjectItem(
                                                    center_item, servo_names[j]);
                                                if (cJSON_IsNumber(center_value)) {
                                                    int center = center_value->valueint;
                                                    if (center >= 0 && center <= 180) {
                                                        center_angle[j] = center;
                                                    }
                                                }
                                            }
                                        }

                                        const int LARGE_AMPLITUDE_THRESHOLD = 40;
                                        bool left_leg_large =
                                            amplitude[LEFT_LEG] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool right_leg_large =
                                            amplitude[RIGHT_LEG] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool left_foot_large =
                                            amplitude[LEFT_FOOT] >= LARGE_AMPLITUDE_THRESHOLD;
                                        bool right_foot_large =
                                            amplitude[RIGHT_FOOT] >= LARGE_AMPLITUDE_THRESHOLD;

                                        if (left_leg_large && right_leg_large) {
                                            ESP_LOGW(TAG,
                                                     "Large oscillation detected on both legs; "
                                                     "limiting right leg amplitude");
                                            amplitude[RIGHT_LEG] = 0;
                                        }
                                        if (left_foot_large && right_foot_large) {
                                            ESP_LOGW(TAG,
                                                     "Large oscillation detected on both feet; "
                                                     "limiting right foot amplitude");
                                            amplitude[RIGHT_FOOT] = 0;
                                        }

                                        cJSON* phase_item = cJSON_GetObjectItem(osc_item, "ph");
                                        if (cJSON_IsObject(phase_item)) {
                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* phase_value =
                                                    cJSON_GetObjectItem(phase_item, servo_names[j]);
                                                if (cJSON_IsNumber(phase_value)) {
                                                    phase_diff[j] = phase_value->valuedouble *
                                                                    3.141592653589793 / 180.0;
                                                }
                                            }
                                        }

                                        cJSON* period_item = cJSON_GetObjectItem(osc_item, "p");
                                        if (cJSON_IsNumber(period_item)) {
                                            period = period_item->valueint;
                                            if (period < 100)
                                                period = 100;
                                            if (period > 3000)
                                                period = 3000;
                                        }

                                        cJSON* steps_item = cJSON_GetObjectItem(osc_item, "c");
                                        if (cJSON_IsNumber(steps_item)) {
                                            steps = (float)steps_item->valuedouble;
                                            if (steps < 0.1)
                                                steps = 0.1;
                                            if (steps > 20.0)
                                                steps = 20.0;
                                        }

                                        ESP_LOGI(TAG,
                                                 "Executing oscillation action %d: period=%d, "
                                                 "steps=%.1f",
                                                 i, period, steps);
                                        controller->mimi_.Execute2(amplitude, center_angle, period,
                                                                   phase_diff, steps);

                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            current_positions[j] = center_angle[j];
                                        }
                                    } else {
                                        int servo_target[SERVO_COUNT];
                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            servo_target[j] = current_positions[j];
                                        }

                                        cJSON* servos_item = cJSON_GetObjectItem(action_item, "s");
                                        if (cJSON_IsObject(servos_item)) {
                                            const char* servo_names[] = {"ll", "rl", "lf",
                                                                         "rf", "lh", "rh"};

                                            for (int j = 0; j < SERVO_COUNT; j++) {
                                                cJSON* servo_value = cJSON_GetObjectItem(
                                                    servos_item, servo_names[j]);
                                                if (cJSON_IsNumber(servo_value)) {
                                                    int position = servo_value->valueint;
                                                    if (position >= 0 && position <= 180) {
                                                        servo_target[j] = position;
                                                    }
                                                }
                                            }
                                        }

                                        int speed = 1000;
                                        cJSON* speed_item = cJSON_GetObjectItem(action_item, "v");
                                        if (cJSON_IsNumber(speed_item)) {
                                            speed = speed_item->valueint;
                                            if (speed < 100)
                                                speed = 100;
                                            if (speed > 3000)
                                                speed = 3000;
                                        }

                                        ESP_LOGI(
                                            TAG,
                                            "Executing action %d: ll=%d, rl=%d, lf=%d, rf=%d, v=%d",
                                            i, servo_target[LEFT_LEG], servo_target[RIGHT_LEG],
                                            servo_target[LEFT_FOOT], servo_target[RIGHT_FOOT],
                                            speed);
                                        controller->mimi_.MoveServos(speed, servo_target);

                                        for (int j = 0; j < SERVO_COUNT; j++) {
                                            current_positions[j] = servo_target[j];
                                        }
                                    }

                                    int delay_after = 0;
                                    cJSON* delay_item = cJSON_GetObjectItem(action_item, "d");
                                    if (cJSON_IsNumber(delay_item)) {
                                        delay_after = delay_item->valueint;
                                        if (delay_after < 0)
                                            delay_after = 0;
                                    }

                                    if (delay_after > 0 && i < array_size - 1) {
                                        ESP_LOGI(TAG, "Action %d completed, delay %d ms", i,
                                                 delay_after);
                                        vTaskDelay(pdMS_TO_TICKS(delay_after));
                                    }
                                }
                            }

                            if (sequence_delay > 0) {
                                UBaseType_t queue_count =
                                    uxQueueMessagesWaiting(controller->action_queue_);
                                if (queue_count > 0) {
                                    ESP_LOGI(TAG,
                                             "Sequence complete, delay %d ms before next sequence "
                                             "(%d queued)",
                                             sequence_delay, queue_count);
                                    vTaskDelay(pdMS_TO_TICKS(sequence_delay));
                                }
                            }
                            cJSON_Delete(json);
                        } else {
                            ESP_LOGE(TAG, "Invalid servo sequence format: 'a' is not an array");
                            cJSON_Delete(json);
                        }
                    } else {
                        const char* error_ptr = cJSON_GetErrorPtr();
                        ESP_LOGE(TAG, "Failed to parse servo sequence JSON, error at: %s",
                                 error_ptr ? error_ptr : "unknown");
                    }
                } else {
                    switch (params.action_type) {
                        case ACTION_WALK:
                            controller->mimi_.Walk(params.steps, params.speed, params.direction,
                                                   params.amount);
                            break;
                        case ACTION_TURN:
                            controller->mimi_.Turn(params.steps, params.speed, params.direction,
                                                   params.amount);
                            break;
                        case ACTION_JUMP:
                            controller->mimi_.Jump(params.steps, params.speed);
                            break;
                        case ACTION_SWING:
                            controller->mimi_.Swing(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_MOONWALK:
                            controller->mimi_.Moonwalker(params.steps, params.speed, params.amount,
                                                         params.direction);
                            break;
                        case ACTION_BEND:
                            controller->mimi_.Bend(params.steps, params.speed, params.direction);
                            break;
                        case ACTION_SHAKE_LEG:
                            controller->mimi_.ShakeLeg(params.steps, params.speed,
                                                       params.direction);
                            break;
                        case ACTION_SIT:
                            controller->mimi_.Sit();
                            break;
                        case ACTION_RADIO_CALISTHENICS:
                            if (controller->has_hands_) {
                                controller->mimi_.RadioCalisthenics();
                            }
                            break;
                        case ACTION_MAGIC_CIRCLE:
                            if (controller->has_hands_) {
                                controller->mimi_.MagicCircle();
                            }
                            break;
                        case ACTION_SHOWCASE:
                            controller->mimi_.Showcase();
                            break;
                        case ACTION_UPDOWN:
                            controller->mimi_.UpDown(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_TIPTOE_SWING:
                            controller->mimi_.TiptoeSwing(params.steps, params.speed,
                                                          params.amount);
                            break;
                        case ACTION_JITTER:
                            controller->mimi_.Jitter(params.steps, params.speed, params.amount);
                            break;
                        case ACTION_ASCENDING_TURN:
                            controller->mimi_.AscendingTurn(params.steps, params.speed,
                                                            params.amount);
                            break;
                        case ACTION_CRUSAITO:
                            controller->mimi_.Crusaito(params.steps, params.speed, params.amount,
                                                       params.direction);
                            break;
                        case ACTION_FLAPPING:
                            controller->mimi_.Flapping(params.steps, params.speed, params.amount,
                                                       params.direction);
                            break;
                        case ACTION_WHIRLWIND_LEG:
                            controller->mimi_.WhirlwindLeg(params.steps, params.speed,
                                                           params.amount);
                            break;
                        case ACTION_HANDS_UP:
                            if (controller->has_hands_) {
                                controller->mimi_.HandsUp(params.speed, params.direction);
                            }
                            break;
                        case ACTION_HANDS_DOWN:
                            if (controller->has_hands_) {
                                controller->mimi_.HandsDown(params.speed, params.direction);
                            }
                            break;
                        case ACTION_HAND_WAVE:
                            if (controller->has_hands_) {
                                controller->mimi_.HandWave(params.direction);
                            }
                            break;
                        case ACTION_WINDMILL:
                            if (controller->has_hands_) {
                                controller->mimi_.Windmill(params.steps, params.speed,
                                                           params.amount);
                            }
                            break;
                        case ACTION_TAKEOFF:
                            if (controller->has_hands_) {
                                controller->mimi_.Takeoff(params.steps, params.speed,
                                                          params.amount);
                            }
                            break;
                        case ACTION_FITNESS:
                            if (controller->has_hands_) {
                                controller->mimi_.Fitness(params.steps, params.speed,
                                                          params.amount);
                            }
                            break;
                        case ACTION_GREETING:
                            if (controller->has_hands_) {
                                controller->mimi_.Greeting(params.direction, params.steps);
                            }
                            break;
                        case ACTION_SHY:
                            if (controller->has_hands_) {
                                controller->mimi_.Shy(params.direction, params.steps);
                            }
                            break;
                        case ACTION_HOME:
                            controller->mimi_.Home(true);
                            break;
                    }
                    if (params.action_type != ACTION_SIT) {
                        if (params.action_type != ACTION_HOME &&
                            params.action_type != ACTION_SERVO_SEQUENCE) {
                            controller->mimi_.Home(params.action_type != ACTION_HANDS_UP);
                        }
                    }
                }
                controller->is_action_in_progress_ = false;
                PowerManager::ResumeBatteryUpdate();
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "mimi_action", 1024 * 3, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    bool IsHandAction(int action_type) const {
        return (action_type >= ACTION_HANDS_UP && action_type <= ACTION_HAND_WAVE) ||
               action_type == ACTION_WINDMILL || action_type == ACTION_TAKEOFF ||
               action_type == ACTION_FITNESS || action_type == ACTION_GREETING ||
               action_type == ACTION_SHY || action_type == ACTION_RADIO_CALISTHENICS ||
               action_type == ACTION_MAGIC_CIRCLE;
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount) {
        if (IsHandAction(action_type)) {
            if (!has_hands_) {
                ESP_LOGW(TAG, "Tried to execute a hand action, but hand servos are not configured");
                return;
            }
        }

        ESP_LOGI(TAG, "Action control: type=%d, steps=%d, speed=%d, direction=%d, amount=%d",
                 action_type, steps, speed, direction, amount);

        MimiActionParams params = {action_type, steps, speed, direction, amount, nullptr};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void LoadTrimsFromNVS() {
        Settings settings("mimi_trims", false);

        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        ESP_LOGI(TAG,
                 "Loaded trims from NVS: left_leg=%d, right_leg=%d, left_foot=%d, right_foot=%d, "
                 "left_hand=%d, right_hand=%d",
                 left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);

        mimi_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);
    }

public:
    MimiController(const HardwareConfig& hw_config) {
        mimi_.Init(hw_config.left_leg_pin, hw_config.right_leg_pin, hw_config.left_foot_pin,
                   hw_config.right_foot_pin, hw_config.left_hand_pin, hw_config.right_hand_pin);

        has_hands_ =
            (hw_config.left_hand_pin != GPIO_NUM_NC && hw_config.right_hand_pin != GPIO_NUM_NC);
        ESP_LOGI(TAG, "Mimi robot initialized %s hand servos", has_hands_ ? "with" : "without");
        ESP_LOGI(TAG, "Servo pin map: LL=%d, RL=%d, LF=%d, RF=%d, LH=%d, RH=%d",
                 hw_config.left_leg_pin, hw_config.right_leg_pin, hw_config.left_foot_pin,
                 hw_config.right_foot_pin, hw_config.left_hand_pin, hw_config.right_hand_pin);

        LoadTrimsFromNVS();

        action_queue_ = xQueueCreate(10, sizeof(MimiActionParams));
        ESP_LOGI(TAG, "Action queue item size: %u bytes", (unsigned)sizeof(MimiActionParams));

        QueueAction(ACTION_HOME, 1, 1000, 1, 0);
    }

    bool ExecuteAction(const std::string& action, int steps, int speed, int direction, int amount,
                       int arm_swing, std::string* error) {
        if (action == "walk") {
            QueueAction(ACTION_WALK, steps, speed, direction, arm_swing);
        } else if (action == "turn") {
            QueueAction(ACTION_TURN, steps, speed, direction, arm_swing);
        } else if (action == "jump") {
            QueueAction(ACTION_JUMP, steps, speed, 0, 0);
        } else if (action == "swing") {
            QueueAction(ACTION_SWING, steps, speed, 0, amount);
        } else if (action == "moonwalk") {
            QueueAction(ACTION_MOONWALK, steps, speed, direction, amount);
        } else if (action == "bend") {
            QueueAction(ACTION_BEND, steps, speed, direction, 0);
        } else if (action == "shake_leg") {
            QueueAction(ACTION_SHAKE_LEG, steps, speed, direction, 0);
        } else if (action == "updown") {
            QueueAction(ACTION_UPDOWN, steps, speed, 0, amount);
        } else if (action == "whirlwind_leg") {
            QueueAction(ACTION_WHIRLWIND_LEG, steps, speed, 0, amount);
        } else if (action == "sit") {
            QueueAction(ACTION_SIT, 1, 0, 0, 0);
        } else if (action == "showcase") {
            QueueAction(ACTION_SHOWCASE, 1, 0, 0, 0);
        } else if (action == "home") {
            QueueAction(ACTION_HOME, 1, 1000, 1, 0);
        } else if (action == "hands_up") {
            return ExecuteHandAction(ACTION_HANDS_UP, 1, speed, direction, 0, error);
        } else if (action == "hands_down") {
            return ExecuteHandAction(ACTION_HANDS_DOWN, 1, speed, direction, 0, error);
        } else if (action == "hand_wave") {
            return ExecuteHandAction(ACTION_HAND_WAVE, 1, 0, 0, direction, error);
        } else if (action == "windmill") {
            return ExecuteHandAction(ACTION_WINDMILL, steps, speed, 0, amount, error);
        } else if (action == "takeoff") {
            return ExecuteHandAction(ACTION_TAKEOFF, steps, speed, 0, amount, error);
        } else if (action == "fitness") {
            return ExecuteHandAction(ACTION_FITNESS, steps, speed, 0, amount, error);
        } else if (action == "greeting") {
            return ExecuteHandAction(ACTION_GREETING, steps, 0, direction, 0, error);
        } else if (action == "shy") {
            return ExecuteHandAction(ACTION_SHY, steps, 0, direction, 0, error);
        } else if (action == "radio_calisthenics") {
            return ExecuteHandAction(ACTION_RADIO_CALISTHENICS, 1, 0, 0, 0, error);
        } else if (action == "magic_circle") {
            return ExecuteHandAction(ACTION_MAGIC_CIRCLE, 1, 0, 0, 0, error);
        } else {
            if (error) {
                *error =
                    "Error: invalid action name. Available actions: walk, turn, jump, swing, "
                    "moonwalk, bend, shake_leg, updown, whirlwind_leg, sit, showcase, home, "
                    "hands_up, hands_down, hand_wave, windmill, takeoff, fitness, greeting, shy, "
                    "radio_calisthenics, magic_circle";
            }
            return false;
        }
        return true;
    }

    bool ExecuteHandAction(int action_type, int steps, int speed, int direction, int amount,
                           std::string* error) {
        if (!has_hands_) {
            if (error) {
                *error = "Error: this action requires hand servos";
            }
            return false;
        }
        QueueAction(action_type, steps, speed, direction, amount);
        return true;
    }

    bool QueueServoSequence(const char* servo_sequence_json) {
        if (servo_sequence_json == nullptr || servo_sequence_json[0] == '\0') {
            ESP_LOGE(TAG, "Sequence JSON is null or empty");
            return false;
        }

        int input_len = strlen(servo_sequence_json);
        ESP_LOGI(TAG, "Queueing servo sequence, len=%d", input_len);

        // Heap-allocate the JSON string; ActionTask will free() it after parsing.
        char* json_copy = (char*)malloc(input_len + 1);
        if (json_copy == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate JSON copy (%d bytes)", input_len + 1);
            return false;
        }
        memcpy(json_copy, servo_sequence_json, input_len + 1);

        MimiActionParams params = {ACTION_SERVO_SEQUENCE, 0, 0, 0, 0, json_copy};
        if (xQueueSend(action_queue_, &params, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGE(TAG, "Action queue full, dropping servo sequence");
            free(json_copy);
            return false;
        }
        StartActionTaskIfNeeded();
        return true;
    }

    void StopActions(bool home = true) {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        is_action_in_progress_ = false;
        PowerManager::ResumeBatteryUpdate();

        // Drain pending items and free heap-allocated JSON pointers before reset.
        // xQueueReset() does not call destructors — must drain manually to avoid leaks.
        MimiActionParams pending;
        while (xQueueReceive(action_queue_, &pending, 0) == pdTRUE) {
            if (pending.servo_sequence_json != nullptr) {
                free(pending.servo_sequence_json);
            }
        }
        xQueueReset(action_queue_);
        if (home) {
            QueueAction(ACTION_HOME, 1, 1000, 1, 0);
        }
    }

    std::string SetTrim(const std::string& servo_type, int trim_value) {
        ESP_LOGI(TAG, "Set servo trim: %s = %d deg", servo_type.c_str(), trim_value);

        Settings settings("mimi_trims", true);
        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        if (servo_type == "left_leg") {
            left_leg = trim_value;
            settings.SetInt("left_leg", left_leg);
        } else if (servo_type == "right_leg") {
            right_leg = trim_value;
            settings.SetInt("right_leg", right_leg);
        } else if (servo_type == "left_foot") {
            left_foot = trim_value;
            settings.SetInt("left_foot", left_foot);
        } else if (servo_type == "right_foot") {
            right_foot = trim_value;
            settings.SetInt("right_foot", right_foot);
        } else if (servo_type == "left_hand") {
            if (!has_hands_) {
                return "Error: robot has no hand servos configured";
            }
            left_hand = trim_value;
            settings.SetInt("left_hand", left_hand);
        } else if (servo_type == "right_hand") {
            if (!has_hands_) {
                return "Error: robot has no hand servos configured";
            }
            right_hand = trim_value;
            settings.SetInt("right_hand", right_hand);
        } else {
            return "Error: invalid servo type. Use: left_leg, right_leg, left_foot, right_foot, "
                   "left_hand, right_hand";
        }

        mimi_.SetTrims(left_leg, right_leg, left_foot, right_foot, left_hand, right_hand);
        QueueAction(ACTION_JUMP, 1, 500, 0, 0);
        return "Servo " + servo_type + " trim set to " + std::to_string(trim_value) +
               " degrees and saved";
    }

    std::string GetTrimsJson() const {
        Settings settings("mimi_trims", false);

        int left_leg = settings.GetInt("left_leg", 0);
        int right_leg = settings.GetInt("right_leg", 0);
        int left_foot = settings.GetInt("left_foot", 0);
        int right_foot = settings.GetInt("right_foot", 0);
        int left_hand = settings.GetInt("left_hand", 0);
        int right_hand = settings.GetInt("right_hand", 0);

        std::string result = "{\"left_leg\":" + std::to_string(left_leg) +
                             ",\"right_leg\":" + std::to_string(right_leg) +
                             ",\"left_foot\":" + std::to_string(left_foot) +
                             ",\"right_foot\":" + std::to_string(right_foot) +
                             ",\"left_hand\":" + std::to_string(left_hand) +
                             ",\"right_hand\":" + std::to_string(right_hand) + "}";

        ESP_LOGI(TAG, "Current trim settings: %s", result.c_str());
        return result;
    }

    const char* GetActionStatus() const { return is_action_in_progress_ ? "moving" : "idle"; }
    bool HasHands() const { return has_hands_; }

    ~MimiController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static MimiController* g_mimi_controller = nullptr;

void InitializeMimiController(const HardwareConfig& hw_config) {
    if (g_mimi_controller == nullptr) {
        g_mimi_controller = new MimiController(hw_config);
        ESP_LOGI(TAG, "Mimi controller initialized");
    }
}

MimiController* GetMimiController() { return g_mimi_controller; }

bool MimiControllerExecuteAction(MimiController* controller,
                                 const std::string& action,
                                 int steps,
                                 int speed,
                                 int direction,
                                 int amount,
                                 int arm_swing,
                                 std::string* error) {
    if (controller == nullptr) {
        if (error) {
            *error = "Error: Mimi controller is not initialized";
        }
        return false;
    }
    return controller->ExecuteAction(action, steps, speed, direction, amount, arm_swing, error);
}

void MimiControllerStopActions(MimiController* controller, bool home) {
    if (controller != nullptr) {
        controller->StopActions(home);
    }
}

const char* MimiControllerGetActionStatus(MimiController* controller) {
    return controller ? controller->GetActionStatus() : "unavailable";
}

bool MimiControllerHasHands(MimiController* controller) {
    return controller && controller->HasHands();
}

#undef TAG
