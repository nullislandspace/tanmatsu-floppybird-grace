#pragma once
// =====================================================================
//  Test kit  --  frame phase timing
// ---------------------------------------------------------------------
//  The same small profiler Stunt Racer uses: named phases wrapped in
//  begin/end pairs at the call sites that matter, accumulated over a
//  reporting period and printed as per-frame milliseconds next to the
//  FPS line. Two microsecond reads per phase per frame -- nothing
//  against the milliseconds being measured.
//
//  The residual ("rest") is the point. Everything the app does is a
//  phase, and so is the engine's present -- the blit and the vsync wait,
//  timed inside se_run and fed in with prof_add(). What is left is the
//  engine's input pump, on_update and the loop itself; if that ever
//  grows, something outside the named phases is eating the frame.
//
//  The phases follow the frame's PPA split (main.c): the FILL is
//  enqueued, the geometry work runs while it is in flight, and only then
//  does the frame wait on it. Submit, prepare and wait are separate
//  phases, not one, precisely so the log shows whether that overlap is
//  paying for itself -- a big "wait" means the fill outlasts the CPU
//  work hiding it.
// =====================================================================

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PROF_FILL = 0,  // queue the backdrop's PPA fills (backdrop_begin)
    PROF_SUBMIT,    // camera + model transform + back-face cull + scene_tri
                    // (which includes the engine's per-face lighting)
    PROF_PREPARE,   // scene_prepare: engine cull + order, no pixels
    PROF_WAIT,      // backdrop_finish: fills still running when the CPU was done, plus the CPU's share (horizon wedge,
                    // fallback)
    PROF_RASTER,    // scene_rasterize: flat triangles, textured ones, then edges
    PROF_BLIT,      // engine present: frame handed to the LCD (se_present_stats)
    PROF_VSYNC,     // engine present: idle until the tearing-effect signal
    PROF_COUNT,
} prof_phase_t;

void prof_begin(prof_phase_t p);
void prof_end(prof_phase_t p);

// Add a duration measured elsewhere -- for work that happens outside
// the app's reach, like the engine's present, which reports its own
// timing after the fact rather than letting a begin/end pair wrap it.
void prof_add(prof_phase_t p, int64_t us);

// Call once per frame, wherever the frame is counted.
void prof_frame(void);

// Drop everything accumulated so far without reporting it -- for
// starting the first period cleanly, so it is not charged with work
// done before the clock started.
void prof_reset(void);

// Per-frame milliseconds for each phase over the period so far, without
// resetting anything (call before prof_flush). Returns the number of
// frames counted; `ms` is left untouched if that is 0.
int prof_snapshot(float ms[PROF_COUNT]);

// Short name of a phase ("fill", "rast", ...), as prof_flush prints it.
char const* prof_name(prof_phase_t p);

// Write a one-line summary of the last reporting period into `out` and
// reset the accumulators: per-frame milliseconds for each phase, then
// the residual against `frame_ms` as "rest". Returns false and writes
// nothing if no frames were counted.
bool prof_flush(char* out, size_t n, float frame_ms);
