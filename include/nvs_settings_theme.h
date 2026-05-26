#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Theme

typedef enum {
    THEME_BLACK,
    THEME_WHITE,
    THEME_RED,
    THEME_BLUE,
    THEME_PURPLE,
    THEME_ORANGE,
    THEME_GREEN,
    THEME_YELLOW,
    THEME_C_BLUE,
    THEME_C_RED,
} theme_setting_t;

esp_err_t nvs_settings_get_theme(theme_setting_t* out_theme);
esp_err_t nvs_settings_set_theme(theme_setting_t theme);