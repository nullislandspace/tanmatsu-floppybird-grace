#pragma once
// =====================================================================
//  Test kit  --  automated device tests, driven over the debug console
// ---------------------------------------------------------------------
//  The host (tools/testrun.py) sends a command; the app runs the test
//  inside its normal frame loop, reports CRC-framed records (report.h)
//  and then returns to the launcher by itself, so a whole build ->
//  install -> run -> test cycle needs nobody at the badge.
//
//    RUN perf scene=<name> [secs=<whole seconds>]
//        Play that piece of content in real time from t = 0 for `secs`
//        seconds (its own duration by default, or 30 s if it is
//        endless). One PERF record per second (phase split, primitive
//        counts, free SRAM), a SHOTPERF record per shot at the end,
//        then END.
//
//    RUN shots scene=<name> ms=<t1>,<t2>,...
//        Render exactly those instants, in milliseconds -- the clock is
//        SET, not run -- and save each to <shot_dir>/<scene>_<ms>.png.
//        A SHOT record per image carries the path and an FNV-1a hash of
//        the framebuffer, so a regression check compares hashes and
//        never has to download an image. Then END.
//
//    EXIT / BADGELINK
//        Return to the launcher now.
//
//  Records: BEGIN, PERF, SHOTPERF, SHOT, END {status: ok|bad|error},
//  BYE (just before the restart).
//
//  WHAT AN APP MUST BE FOR THIS TO MEAN ANYTHING
//
//  The shots test only works if what is drawn is a pure function of the
//  clock: same t, same picture, every time. Then a reference hash keeps
//  its meaning, a host-side checker can replay the same instant, and a
//  video export can render slower than real time without changing what
//  it shows. An app whose frame depends on accumulated dt, on input
//  timing, or on anything random it does not seed can still use the perf
//  test, but its shot hashes will wobble and the references are worth
//  nothing.
//
//  The kit owns that clock: showtime.h. The app calls showtime_frame()
//  once per frame and draws from showtime_now(); the kit sets it.
// =====================================================================

#include <stdbool.h>
#include <stdint.h>
#include "debugcon.h"
#include "pax_gfx.h"

// How the tests address the app's content. For a showreel that is a
// scene of the playlist; for a game, a level, a screen, a replay -- the
// kit only needs to be able to name one, start it, and ask how far in
// it is. Every function is required.
typedef struct {
    // Play `name` from its start and stay on it (no advancing to the
    // next thing) until the app is restarted. False: no such content,
    // and the test ends with an error naming it.
    bool (*select)(char const* name);
    // How long the selected content runs, in seconds. <= 0 means
    // endless, and a perf test then uses its own default.
    float (*duration)(void);
    // The show time (showtime_now()) at which the selected content
    // started, i.e. its own t = 0.
    double (*started)(void);
    // What is selected right now, for the identity record.
    char const* (*name)(void);
    // The current shot/section within the content, for per-shot
    // statistics; return "" if the app has no such division.
    char const* (*shot_name)(void);
} devtest_content_t;

typedef struct {
    char const*              app;       // slug, for the identity record
    char const*              shot_dir;  // e.g. "/sd/myapp/test"; created if absent
    devtest_content_t const* content;
    // Called when a test begins, so the app's own frame statistics start
    // clean with it. May be NULL.
    void (*stats_restart)(void);
} devtest_config_t;

// Start the console listener and arm the tests. `cfg` (and everything
// it points at) must outlive the call: pass a static.
void devtest_start(devtest_config_t const* cfg);

// Per frame, in on_update AFTER showtime_frame() and BEFORE the app
// advances its own content: takes commands and steers the clock.
void devtest_update(void);

// Per frame, in on_render after the frame is finished (for the engine,
// after scene_rasterize()): captures shots, accumulates perf.
// `rast_us` is this frame's rasterize time, or 0 if the app has no
// meaningful equivalent.
void devtest_after_render(pax_buf_t* fb, int64_t rast_us);

// Once per statistics period, before the app logs its own line: emits a
// PERF record while a perf test runs.
void devtest_period(float fps, float frame_ms);
