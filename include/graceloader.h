// SPDX-License-Identifier: MIT
// Graceloader API — functions exported to dynamically-loaded apps
#pragma once

#include "esp_err.h"
#include "soc/soc_caps.h"
#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns the install base path of the currently running app.
// For example, if the app was loaded from /int/apps/tld.username.myapp/app.so,
// this returns "/int/apps/tld.username.myapp".
// The returned string is valid for the lifetime of the app.
const char* graceloader_get_install_basepath(void);

// Audio volume limit.
// While an app runs under graceloader, bsp_audio_set_volume() scales the requested 0..100% down to
// 0..GRACELOADER_VOLUME_LIMIT_PERCENT at the codec, and bsp_audio_get_volume() reports the value back on the
// app's 0..100% scale. With badge-bsp 1.5.0, 75% is 0 dB; higher settings only add digital gain and clipping.
// Pass the launcher's speaker/headphone volume setting unchanged; do not compensate for the scaling.
// Calling the ES8156 driver directly (es8156_set_volume_percentage etc.) bypasses the limit.
#define GRACELOADER_VOLUME_LIMIT_PERCENT 75

// MIPI DPI panel event callbacks (Tanmatsu display).
// The display driver only accepts IRAM callbacks, and app code is in PSRAM, so apps cannot call
// esp_lcd_dpi_panel_register_event_callbacks() themselves. Register them here instead: graceloader's own IRAM
// handlers call the BSP's callbacks first, then these. The panel handle passed to them is the DPI panel.
//
// - They run in interrupt context: no blocking, use the ...FromISR FreeRTOS calls, return true if a
//   higher-priority task was woken.
// - They are SKIPPED while the cache is disabled (a flash write/erase is in progress). The app's tasks cannot
//   run then either, and the display keeps refreshing, so the first refresh after the flash operation calls
//   them again: a skipped event delays a waiter by at most one refresh. Still wait with a timeout, in case the
//   display is not refreshing at all.
// - on_refresh_done fires once per display refresh (enabling it enables a per-frame interrupt).
// - Page flipping: with two frame buffers (num_fbs = 2 in the BSP display configuration; get them with
//   esp_lcd_dpi_panel_get_frame_buffer()), passing one of them to esp_lcd_panel_draw_bitmap() does not copy, it
//   only selects that buffer for the next refresh. The other buffer is free once an on_refresh_done has fired
//   AFTER that draw_bitmap call. With a binary semaphore given by on_refresh_done:
//       esp_lcd_panel_draw_bitmap(panel, 0, 0, w, h, back);   // select the new buffer
//       xSemaphoreTake(refresh_sem, 0);                      // drop a give from before the selection
//       xSemaphoreTake(refresh_sem, pdMS_TO_TICKS(100));     // the old buffer is free now
//   Clear the semaphore after draw_bitmap, not before: a refresh in between already shows the new buffer, so
//   dropping its give only costs a frame, while clearing first lets a refresh of the old buffer end the wait.
//   This holds for ESP32-P4 builds below chip revision 3.0 (graceloader's), where on_refresh_done comes from the
//   same interrupt that restarts the refresh with the selected buffer. From revision 3.0 on it comes from the
//   DSI bridge's vsync, slightly after that restart, and a draw_bitmap in between would end the wait while the
//   old buffer is being scanned out again: wait for two on_refresh_done there.
// - Register after bsp_device_initialize() or before, either works. Pass NULL to unregister.
#if SOC_MIPI_DSI_SUPPORTED
esp_err_t graceloader_display_register_callbacks(esp_lcd_dpi_panel_event_callbacks_t const* cbs, void* user_ctx);
#endif

#ifdef __cplusplus
}
#endif
