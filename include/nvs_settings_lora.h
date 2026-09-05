#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// LoRa settings

esp_err_t nvs_settings_get_lora_frequency(uint32_t* out_value, uint32_t default_value);
esp_err_t nvs_settings_set_lora_frequency(uint32_t frequency);
esp_err_t nvs_settings_get_lora_spreading_factor(uint8_t* out_sf);
esp_err_t nvs_settings_set_lora_spreading_factor(uint8_t sf);
esp_err_t nvs_settings_get_lora_bandwidth(uint16_t* out_bandwidth);
esp_err_t nvs_settings_set_lora_bandwidth(uint16_t bandwidth);
esp_err_t nvs_settings_get_lora_coding_rate(uint8_t* out_coding_rate);
esp_err_t nvs_settings_set_lora_coding_rate(uint8_t coding_rate);
esp_err_t nvs_settings_get_lora_power(uint8_t* out_power);
esp_err_t nvs_settings_set_lora_power(uint8_t power);
esp_err_t nvs_settings_get_lora_offset(int32_t* out_offset);
esp_err_t nvs_settings_set_lora_offset(int32_t offset);
esp_err_t nvs_settings_get_lora_automatic_offset(bool* out_enabled);
esp_err_t nvs_settings_set_lora_automatic_offset(bool enabled);
esp_err_t nvs_settings_get_lora_low_data_rate_optimization(bool* out_enabled);
esp_err_t nvs_settings_set_lora_low_data_rate_optimization(bool enabled);
bool      nvs_settings_get_lora_configured(void);
