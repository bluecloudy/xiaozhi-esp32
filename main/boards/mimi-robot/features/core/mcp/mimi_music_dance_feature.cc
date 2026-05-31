#include "mcp_server.h"
#include "../controller/mimi_controller.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstring>
#include <string>

#define TAG "MimiMusicDance"

class Esp32Music;

extern "C" const char* BoardMusicDancePlayAudio(Esp32Music* music,
                                                const char* url,
                                                const char* title,
                                                const char* artist,
                                                const char* source_name,
                                                const char* lyric_url,
                                                int duration_ms,
                                                const char* decoder);
extern "C" void BoardMusicDanceStopStreaming(Esp32Music* music);
extern "C" bool BoardMusicDanceIsActive(Esp32Music* music);
extern "C" void BoardMusicDanceSetEndCallback(Esp32Music* music,
                                              void (*callback)(void*),
                                              void* context);

namespace {

struct DanceAction {
    const char* action;
    int steps;
    int speed;
    int direction;
    int amount;
    int arm_swing;
    bool requires_hands;
};

constexpr DanceAction kRoutine[] = {
    // Leg-only moves — always execute
    {"moonwalk",      3, 700,  1,  30, 50, false},
    {"swing",         3, 700,  1,  35, 50, false},
    {"updown",        3, 700,  1,  35, 50, false},
    {"jitter",        3, 500,  1,  20, 50, false},
    {"jump",          2, 700,  1,  30, 50, false},
    {"whirlwind_leg", 1, 900,  1,  20, 50, false},
    // Arm moves — only when has_hands
    {"hand_wave",     1, 700,  1,  30, 50, true},
    {"windmill",      3, 700,  1,  70, 50, true},
    {"takeoff",       3, 400,  1,  40, 50, true},
    {"hand_wave",     1, 700, -1,  30, 50, true},
};

std::atomic<bool> g_dance_active{false};
TaskHandle_t g_dance_task = nullptr;
Esp32Music* g_music = nullptr;

cJSON* Result(bool success, const char* message) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "success", success);
    cJSON_AddStringToObject(json, "message", message);
    return json;
}

void StopDance(bool home) {
    bool was_active = g_dance_active.exchange(false);

    if (g_dance_task != nullptr && g_dance_task != xTaskGetCurrentTaskHandle()) {
        TaskHandle_t task = g_dance_task;
        g_dance_task = nullptr;
        vTaskDelete(task);
    }

    if (home && was_active) {
        MimiControllerStopActions(GetMimiController());
    }
}

void StopAll() {
    if (g_music != nullptr) {
        BoardMusicDanceStopStreaming(g_music);
    }
    StopDance(true);
}

void DanceTask(void*) {
    MimiController* controller = GetMimiController();
    size_t index = 0;

    while (g_dance_active.load()) {
        if (g_music == nullptr || !BoardMusicDanceIsActive(g_music)) {
            break;
        }

        const char* status = MimiControllerGetActionStatus(controller);
        if (std::strcmp(status, "idle") == 0) {
            const DanceAction& action = kRoutine[index % (sizeof(kRoutine) / sizeof(kRoutine[0]))];
            index++;

            if (action.requires_hands && !MimiControllerHasHands(controller)) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            std::string error;
            if (!MimiControllerExecuteAction(controller,
                                             action.action,
                                             action.steps,
                                             action.speed,
                                             action.direction,
                                             action.amount,
                                             action.arm_swing,
                                             &error)) {
                ESP_LOGW(TAG, "Dance action failed: %s", error.c_str());
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }

    g_dance_active = false;
    g_dance_task = nullptr;
    MimiControllerStopActions(controller);
    vTaskDelete(nullptr);
}

void StartDance() {
    StopDance(true);
    g_dance_active = true;
    if (xTaskCreate(DanceTask, "mimi_music_dance", 4096, nullptr, 4, &g_dance_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create music dance task");
        g_dance_active = false;
        MimiControllerStopActions(GetMimiController());
    }
}

cJSON* PlayMusicAndDance(Esp32Music* music, const PropertyList& properties) {
    if (music == nullptr) {
        return Result(false, "Music player is not initialized");
    }
    if (GetMimiController() == nullptr) {
        return Result(false, "Mimi controller is not initialized");
    }

    std::string url = properties["url"].value<std::string>();
    std::string title = properties["title"].value<std::string>();
    std::string artist = properties["artist"].value<std::string>();
    std::string source_name = properties["source_name"].value<std::string>();
    std::string lyric_url = properties["lyric_url"].value<std::string>();
    int duration_ms = properties["duration_ms"].value<int>();
    std::string decoder = properties["decoder"].value<std::string>();

    if (url.empty()) {
        return Result(false, "Audio URL is empty");
    }

    StopAll();
    g_music = music;
    BoardMusicDanceSetEndCallback(music, [](void*) {
        StopDance(true);
    }, nullptr);

    const char* error = BoardMusicDancePlayAudio(music,
                                                 url.c_str(),
                                                 title.c_str(),
                                                 artist.c_str(),
                                                 source_name.c_str(),
                                                 lyric_url.c_str(),
                                                 duration_ms,
                                                 decoder.c_str());
    if (error != nullptr) {
        StopDance(true);
        return Result(false, error);
    }

    StartDance();
    return Result(true, "Music started and Mimi dance routine started");
}

} // namespace

extern "C" void RegisterBoardMusicDanceTools(Esp32Music* music) {
    if (music == nullptr) return;

    g_music = music;
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.music_dance.play_url",
        "Play online music and make Mimi dance while the music is playing.\n"
        "For requests like 'phat nhac Son Tung va nhay theo dieu nhac', first resolve the music "
        "with remote `mimi_play(media_type=\"music\", query=<user text>, quality=\"128\")`. "
        "If only `item_id` is returned by `mimi_play` or `mimi_search`, call remote "
        "`mimi_stream(media_type=\"music\", item_id=<id>, quality=\"128\")`. "
        "Then call this local tool with `data.audio_url`, falling back to `data.stream_url`. "
        "Do not invent URLs. When the user asks to stop music or dancing, call "
        "`self.music_dance.stop`.\n"
        "Args: url, title, artist, source_name, lyric_url, duration_ms, decoder.",
        PropertyList({
            Property("url", kPropertyTypeString),
            Property("title", kPropertyTypeString, ""),
            Property("artist", kPropertyTypeString, ""),
            Property("source_name", kPropertyTypeString, ""),
            Property("lyric_url", kPropertyTypeString, ""),
            Property("duration_ms", kPropertyTypeInteger, 0),
            Property("decoder", kPropertyTypeString, "auto")
        }),
        [music](const PropertyList& properties) -> ReturnValue {
            return PlayMusicAndDance(music, properties);
        });

    mcp.AddTool("self.music_dance.stop",
        "Stop music-dance mode: stop online music playback and stop Mimi actions.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            StopAll();
            return Result(true, "Music dance stopped");
        });

    ESP_LOGI(TAG, "Mimi music-dance MCP tools registered");
}

#undef TAG
