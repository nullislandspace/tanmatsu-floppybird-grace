// gl_input.h - Graceloader merged input API for apps
//
// Apps should call these functions instead of the bsp_input_* equivalents.
// They behave identically to the BSP functions but transparently merge
// input from every source the graceloader knows about — the on-board
// Tanmatsu coprocessor keyboard, a USB HID keyboard plugged into the
// USB-A port, and (in future) gamepads or other peripherals.
//
// The event queue returned by gl_input_get_queue receives SCANCODE,
// NAVIGATION and KEYBOARD (ASCII) events from every source. The polled
// gl_input_read_* functions return true if any source currently holds
// the key.

#pragma once

#include "bsp/input.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the graceloader input system. Called once by graceloader
// before the app entry point — apps do not need to call this.
//
// Brings up USB host + HID host on Core 0 so a plugged-in USB keyboard
// will inject events alongside the native keyboard.
esp_err_t gl_input_init(void);

// Get the merged event queue. Drop-in replacement for bsp_input_get_queue.
esp_err_t gl_input_get_queue(QueueHandle_t* out_queue);

// Read the merged held-state of a navigation key. Returns true if
// the native keyboard OR any connected USB keyboard currently holds
// the key. Drop-in replacement for bsp_input_read_navigation_key.
esp_err_t gl_input_read_navigation_key(bsp_input_navigation_key_t key, bool* out_state);

// Read the merged held-state of a scancode. Returns true if the native
// keyboard OR any connected USB keyboard currently holds the key.
// Drop-in replacement for bsp_input_read_scancode.
esp_err_t gl_input_read_scancode(bsp_input_scancode_t key, bool* out_state);

// Read the current state of an action. Pass-through to bsp_input_read_action
// today — USB devices don't generate action events.
esp_err_t gl_input_read_action(bsp_input_action_type_t action, bool* out_state);

#ifdef __cplusplus
}
#endif
