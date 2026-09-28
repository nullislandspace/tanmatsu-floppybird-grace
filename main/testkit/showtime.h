#pragma once
// =====================================================================
//  Test kit  --  the show clock
// ---------------------------------------------------------------------
//  The ONLY place the reel reads "what time is it". Every scene and
//  asset is a pure function of the time this returns (never of an
//  accumulated per-frame dt), so a slow or dropped frame just samples
//  the choreography a little later instead of drifting, and two runs
//  that ask for the same instant draw the same picture.
//
//  Two modes:
//
//    realtime     the wall clock (esp_timer), minus any stall the app
//                 asked to exclude (a screenshot's SD write). Default.
//    fixed step   every showtime_frame() advances exactly 1/fps,
//                 whatever the wall clock does -- for rendering a
//                 constant-rate video (e.g. a 30 fps MJPEG) far slower
//                 than real time, and for the automated shot tests.
//
//  Profiling does NOT use this: it measures real time on purpose.
// =====================================================================

#include <stdint.h>

// Latch this frame's time. Call exactly once per frame, before anything
// reads showtime_now(), so every object in a frame sees the same instant.
void showtime_frame(void);

// This frame's show time, in seconds. Constant between two
// showtime_frame() calls.
double showtime_now(void);

// Switch to the wall clock. Time continues from the current show time,
// so switching never makes the show jump.
void showtime_set_realtime(void);

// Switch to fixed stepping: each following showtime_frame() adds
// exactly 1/fps seconds. Also continues from the current show time.
void showtime_set_fixed_step(float fps);

// Jump to show time t (either mode). Takes effect immediately:
// showtime_now() returns t until the next showtime_frame() advances it.
void showtime_set(double t);

// Take `us` microseconds of wall clock out of the realtime show clock,
// e.g. a stall the app caused on purpose and does not want the show to
// jump over. No effect in fixed-step mode (it never sees wall time).
void showtime_exclude(int64_t us);
