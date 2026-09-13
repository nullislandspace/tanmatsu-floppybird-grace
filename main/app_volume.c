#include "app_volume.h"
#include "bsp/audio.h"
#include "esp_log.h"
#include "gl_input.h"
#include "nvs_settings_hardware.h"

// Same default and step size as the launcher
#define VOLUME_DEFAULT_PERCENT 50
#define VOLUME_STEP_PERCENT    5

static char const TAG[] = "app_volume";

static bool    headphones_inserted = false;
static uint8_t current_volume      = VOLUME_DEFAULT_PERCENT;

// Read the active output's persisted volume from the launcher's settings
static uint8_t read_active_volume(void) {
    uint8_t v = VOLUME_DEFAULT_PERCENT;
    if (headphones_inserted) {
        nvs_settings_get_headphone_volume(&v, VOLUME_DEFAULT_PERCENT);
    } else {
        nvs_settings_get_speaker_volume(&v, VOLUME_DEFAULT_PERCENT);
    }
    if (v > 100) v = 100;
    return v;
}

static void apply_routing(void) {
    current_volume = read_active_volume();
    bsp_audio_set_volume((float)current_volume);
    // Speaker amplifier off while headphones are plugged in
    bsp_audio_set_amplifier(!headphones_inserted);
    ESP_LOGI(TAG, "%s, volume %u%%", headphones_inserted ? "headphones" : "speaker", (unsigned)current_volume);
}

static void step_volume(int delta) {
    int next = (int)read_active_volume() + delta;
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    current_volume = (uint8_t)next;

    esp_err_t res = headphones_inserted ? nvs_settings_set_headphone_volume(current_volume)
                                        : nvs_settings_set_speaker_volume(current_volume);
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store volume: %d", res);
    }
    bsp_audio_set_volume((float)current_volume);
    ESP_LOGI(TAG, "Volume %u%%", (unsigned)current_volume);
}

void app_volume_init(void) {
    bool inserted = false;
    if (gl_input_read_action(BSP_INPUT_ACTION_TYPE_AUDIO_JACK, &inserted) != ESP_OK) {
        inserted = false;
    }
    headphones_inserted = inserted;
    apply_routing();
}

bool app_volume_handle_event(bsp_input_event_t const* event) {
    switch (event->type) {
        case INPUT_EVENT_TYPE_ACTION:
            if (event->args_action.type != BSP_INPUT_ACTION_TYPE_AUDIO_JACK) return false;
            if (event->args_action.state != headphones_inserted) {
                headphones_inserted = event->args_action.state;
                apply_routing();
            }
            return true;
        case INPUT_EVENT_TYPE_NAVIGATION:
            if (event->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_VOLUME_UP) {
                if (event->args_navigation.state) step_volume(VOLUME_STEP_PERCENT);
                return true;
            }
            if (event->args_navigation.key == BSP_INPUT_NAVIGATION_KEY_VOLUME_DOWN) {
                if (event->args_navigation.state) step_volume(-VOLUME_STEP_PERCENT);
                return true;
            }
            return false;
        case INPUT_EVENT_TYPE_SCANCODE: {
            // The volume keys also send scancodes; swallow them so they don't count as regular key presses
            uint32_t key = event->args_scancode.scancode & ~BSP_INPUT_SCANCODE_RELEASE_MODIFIER;
            return key == BSP_INPUT_SCANCODE_ESCAPED_VOLUME_UP || key == BSP_INPUT_SCANCODE_ESCAPED_VOLUME_DOWN;
        }
        default:
            return false;
    }
}

uint8_t app_volume_get(void) {
    return current_volume;
}
