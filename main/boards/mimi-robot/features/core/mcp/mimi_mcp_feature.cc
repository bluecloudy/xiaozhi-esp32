#include "x_feature.h"

#include <wifi_manager.h>
#include "board.h"
#include "mcp_server.h"

#include <esp_log.h>

#include <memory>
#include <string>

#define TAG "MimiMcpFeature"

class MimiMcpFeature : public XFeature {
public:
    void Initialize() override {
        auto* controller = GetMimiController();
        if (controller == nullptr) {
            ESP_LOGE(TAG, "Mimi controller is not initialized");
            return;
        }

        RegisterRobotTools(*controller);
        RegisterBoardStatusTools();
        ESP_LOGI(TAG, "Mimi MCP tools registered");
    }

private:
    static void RegisterRobotTools(MimiController& controller) {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.mimi.action",
            "Execute robot actions. action: action name. Parameters by action type: direction (1 "
            "forward/left, -1 backward/right, 0 both), "
            "steps (1-100), speed (100-3000, lower is faster), amount (0-170), arm_swing (0-170). "
            "Basic: walk, turn, jump, swing, moonwalk, bend, shake_leg, updown, whirlwind_leg. "
            "Fixed: sit, showcase, home. "
            "Hand actions (require hand servos): hands_up, hands_down, hand_wave, windmill, "
            "takeoff, fitness, greeting, shy, radio_calisthenics, magic_circle.",
            PropertyList({Property("action", kPropertyTypeString, "sit"),
                          Property("steps", kPropertyTypeInteger, 3, 1, 100),
                          Property("speed", kPropertyTypeInteger, 700, 100, 3000),
                          Property("direction", kPropertyTypeInteger, 1, -1, 1),
                          Property("amount", kPropertyTypeInteger, 30, 0, 170),
                          Property("arm_swing", kPropertyTypeInteger, 50, 0, 170)}),
            [&controller](const PropertyList& properties) -> ReturnValue {
                std::string action = properties["action"].value<std::string>();
                int steps = properties["steps"].value<int>();
                int speed = properties["speed"].value<int>();
                int direction = properties["direction"].value<int>();
                int amount = properties["amount"].value<int>();
                int arm_swing = properties["arm_swing"].value<int>();
                std::string error;

                if (!controller.ExecuteAction(action, steps, speed, direction, amount, arm_swing,
                                              &error)) {
                    return error;
                }
                return true;
            });

        mcp_server.AddTool(
            "self.mimi.servo_sequences",
            "Custom AI motion programming. Supports segmented calls: when more than five sequences "
            "are needed, call this tool multiple times with short sequences and they will be "
            "queued automatically. "
            "Supports two modes: standard move mode and oscillator mode. "
            "Robot structure: two hands (up/down), two legs (in/out), two feet (up/down). "
            "Servo keys: ll/rl/lf/rf/lh/rh. "
            "sequence: one sequence object with top-level 'a' action array and optional top-level "
            "'d' delay after sequence completion. "
            "Each action: standard mode uses 's' servo targets (0-180), optional 'v' speed "
            "(100-3000, default 1000), optional 'd' delay after action. "
            "Oscillator mode uses 'osc' with optional 'a' amplitudes (10-90), 'o' center angles "
            "(0-180), 'ph' phase offsets in degrees, 'p' period (100-3000), and 'c' cycle count "
            "(0.1-20.0). "
            "Safety note: when oscillating legs/feet, keep one foot at 90 degrees to avoid "
            "hardware damage. If multiple sequences are sent and a final reset is needed, call "
            "self.mimi.action with {\"action\":\"home\"} at the end instead of embedding reset "
            "behavior in the sequence.",
            PropertyList({Property("sequence", kPropertyTypeString,
                                   "{\"a\":[{\"s\":{\"ll\":90,\"rl\":90},\"v\":1000}]}")}),
            [&controller](const PropertyList& properties) -> ReturnValue {
                std::string sequence = properties["sequence"].value<std::string>();
                return controller.QueueServoSequence(sequence.c_str());
            });

        mcp_server.AddTool("self.mimi.stop", "Stop all actions immediately and return home",
                           PropertyList(),
                           [&controller](const PropertyList& properties) -> ReturnValue {
                               (void)properties;
                               controller.StopActions();
                               return true;
                           });

        mcp_server.AddTool(
            "self.mimi.set_trim",
            "Calibrate one servo trim. Updates the selected servo trim to adjust home stance and "
            "saves it persistently. "
            "servo_type: left_leg/right_leg/left_foot/right_foot/left_hand/right_hand; trim_value: "
            "-50 to 50 degrees.",
            PropertyList({Property("servo_type", kPropertyTypeString, "left_leg"),
                          Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
            [&controller](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();
                return controller.SetTrim(servo_type, trim_value);
            });

        mcp_server.AddTool("self.mimi.get_trims", "Get current servo trim settings", PropertyList(),
                           [&controller](const PropertyList& properties) -> ReturnValue {
                               (void)properties;
                               return controller.GetTrimsJson();
                           });

        mcp_server.AddTool("self.mimi.get_status", "Get robot action status: moving or idle",
                           PropertyList(),
                           [&controller](const PropertyList& properties) -> ReturnValue {
                               (void)properties;
                               return controller.GetActionStatus();
                           });

        mcp_server.AddTool(
            "self.mimi.test_servo",
            "Test a single servo by moving it to a target angle. Use this to verify servo hardware "
            "is working correctly. "
            "servo: servo key to test — ll (left leg), rl (right leg), lf (left foot), "
            "rf (right foot), lh (left hand), rh (right hand). "
            "angle: target angle in degrees (0-180, default 90 = center). "
            "speed: movement speed (100-3000, lower is faster, default 800). "
            "reset: if true, return servo to center (90°) after 1 second.",
            PropertyList({Property("servo", kPropertyTypeString, "ll"),
                          Property("angle", kPropertyTypeInteger, 90, 0, 180),
                          Property("speed", kPropertyTypeInteger, 800, 100, 3000),
                          Property("reset", kPropertyTypeBoolean, false)}),
            [&controller](const PropertyList& properties) -> ReturnValue {
                std::string servo = properties["servo"].value<std::string>();
                int angle = properties["angle"].value<int>();
                int speed = properties["speed"].value<int>();
                bool reset = properties["reset"].value<bool>();

                // Validate servo key
                const char* valid_keys[] = {"ll", "rl", "lf", "rf", "lh", "rh"};
                bool valid = false;
                for (auto& key : valid_keys) {
                    if (servo == key) {
                        valid = true;
                        break;
                    }
                }
                if (!valid) {
                    return std::string("Error: invalid servo key '") + servo +
                           "'. Valid keys: ll, rl, lf, rf, lh, rh";
                }

                char json_buf[256];
                if (reset) {
                    // Two-action sequence: move to angle, then reset to 90
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"a\":["
                             "{\"s\":{\"%s\":%d},\"v\":%d,\"d\":1000},"
                             "{\"s\":{\"%s\":90},\"v\":%d}"
                             "]}",
                             servo.c_str(), angle, speed, servo.c_str(), speed);
                } else {
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"a\":[{\"s\":{\"%s\":%d},\"v\":%d}]}",
                             servo.c_str(), angle, speed);
                }

                ESP_LOGI(TAG, "Test servo '%s' -> angle=%d speed=%d reset=%s",
                         servo.c_str(), angle, speed, reset ? "yes" : "no");
                return controller.QueueServoSequence(json_buf);
            });
    }

    static void RegisterBoardStatusTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("self.battery.get_level", "Get robot battery level and charging state",
                           PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                               (void)properties;
                               auto& board = Board::GetInstance();
                               int level = 0;
                               bool charging = false;
                               bool discharging = false;
                               board.GetBatteryLevel(level, charging, discharging);

                               return "{\"level\":" + std::to_string(level) +
                                      ",\"charging\":" + (charging ? "true" : "false") + "}";
                           });

        mcp_server.AddTool("self.mimi.get_ip", "Get robot Wi-Fi IP address", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               (void)properties;
                               auto& wifi = WifiManager::GetInstance();
                               std::string ip = wifi.GetIpAddress();
                               if (ip.empty()) {
                                   return "{\"ip\":\"\",\"connected\":false}";
                               }
                               return "{\"ip\":\"" + ip + "\",\"connected\":true}";
                           });
    }
};

std::unique_ptr<XFeature> CreateMimiMcpFeature() { return std::make_unique<MimiMcpFeature>(); }

#undef TAG
