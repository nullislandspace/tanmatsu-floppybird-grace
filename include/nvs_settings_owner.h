#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Owner settings

esp_err_t nvs_settings_get_owner_nickname(char* out_value, size_t max_length, const char* default_value);
esp_err_t nvs_settings_set_owner_nickname(const char* value);
bool      nvs_settings_get_owner_nickname_configured(void);
esp_err_t nvs_settings_get_owner_birthday_day(uint8_t* out_day, uint8_t default_value);
esp_err_t nvs_settings_set_owner_birthday_day(uint8_t day);
esp_err_t nvs_settings_get_owner_birthday_month(uint8_t* out_month, uint8_t default_value);
esp_err_t nvs_settings_set_owner_birthday_month(uint8_t month);
bool      nvs_settings_get_owner_birthday_configured(void);
