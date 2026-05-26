#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Hardware settings

esp_err_t nvs_settings_get_display_brightness(uint8_t* out_percentage, uint8_t default_value);
esp_err_t nvs_settings_set_display_brightness(uint8_t percentage);
esp_err_t nvs_settings_get_keyboard_brightness(uint8_t* out_percentage, uint8_t default_value);
esp_err_t nvs_settings_set_keyboard_brightness(uint8_t percentage);
esp_err_t nvs_settings_get_led_brightness(uint8_t* out_percentage, uint8_t default_value);
esp_err_t nvs_settings_set_led_brightness(uint8_t percentage);
esp_err_t nvs_settings_get_speaker_volume(uint8_t* out_percentage, uint8_t default_value);
esp_err_t nvs_settings_set_speaker_volume(uint8_t percentage);
esp_err_t nvs_settings_get_headphone_volume(uint8_t* out_percentage, uint8_t default_value);
esp_err_t nvs_settings_set_headphone_volume(uint8_t percentage);
