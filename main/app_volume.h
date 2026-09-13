#pragma once

// Device-global audio volume, shared with the launcher and other apps.
//
// The launcher persists separate speaker and headphone volumes in NVS. This module applies the one matching
// the current audio jack state, handles the volume keys (5% steps, written back to NVS so every app and the
// launcher stay in sync) and switches the speaker amplifier and volume when headphones are plugged or unplugged.

#include <stdbool.h>
#include <stdint.h>
#include "bsp/input.h"

// Read the jack state, enable/disable the speaker amplifier and apply the persisted volume.
// Call after the BSP audio subsystem has been initialized (bsp_audio_initialize resets the codec volume).
void app_volume_init(void);

// Offer an input event to the volume handler. Returns true if the event was a volume key (press, release,
// navigation or scancode) or an audio jack event; the app should then ignore it.
bool app_volume_handle_event(bsp_input_event_t const* event);

// Currently applied volume of the active output in percent.
uint8_t app_volume_get(void);
