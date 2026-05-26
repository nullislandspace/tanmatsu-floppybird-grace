#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Repository settings

esp_err_t nvs_settings_get_repo_server(char* out_value, size_t max_length, const char* default_value);
esp_err_t nvs_settings_set_repo_server(const char* value);
esp_err_t nvs_settings_get_repo_base_uri(char* out_value, size_t max_length, const char* default_value);
esp_err_t nvs_settings_set_repo_base_uri(const char* value);
esp_err_t nvs_settings_get_http_user_agent(char* out_value, size_t max_length, const char* default_value);
esp_err_t nvs_settings_set_http_user_agent(const char* value);
