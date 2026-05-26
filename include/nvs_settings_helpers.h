#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Generic helper functions for accessing settings stored in NVS

esp_err_t nvs_settings_get_u8(const char* key, uint8_t default_value, uint8_t* out_value);
esp_err_t nvs_settings_set_u8(const char* key, uint8_t value);
esp_err_t nvs_settings_get_u16(const char* key, uint16_t default_value, uint16_t* out_value);
esp_err_t nvs_settings_set_u16(const char* key, uint16_t value);
esp_err_t nvs_settings_get_u32(const char* key, uint32_t default_value, uint32_t* out_value);
esp_err_t nvs_settings_set_u32(const char* key, uint32_t value);
esp_err_t nvs_settings_get_percentage(const char* key, uint8_t default_value, uint8_t* out_percentage);
esp_err_t nvs_settings_set_percentage(const char* key, uint8_t percentage);
esp_err_t nvs_settings_get_string(const char* key, const char* default_value, char* out_value, size_t max_length);
esp_err_t nvs_settings_set_string(const char* key, const char* value);
