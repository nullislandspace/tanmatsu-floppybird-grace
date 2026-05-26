#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Firmware settings

esp_err_t nvs_settings_get_firmware_patch_level(uint8_t* out_level);
esp_err_t nvs_settings_set_firmware_patch_level(uint8_t level);
