#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Meshcore settings

esp_err_t nvs_settings_get_meshcore_name(char* out_value, size_t max_length);
esp_err_t nvs_settings_set_meshcore_name(const char* value);
esp_err_t nvs_settings_get_meshcore_private_key(char* out_value, size_t max_length);
esp_err_t nvs_settings_set_meshcore_private_key(const char* value);
esp_err_t nvs_settings_get_meshcore_public_key(char* out_value, size_t max_length);
esp_err_t nvs_settings_set_meshcore_public_key(const char* value);
