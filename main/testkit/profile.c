// =====================================================================
//  Test kit  --  frame phase timing (see profile.h)
// =====================================================================

#include "profile.h"
#include <stdio.h>
#include "esp_timer.h"

static char const* const NAMES[PROF_COUNT] = {
    [PROF_FILL] = "fill",   [PROF_SUBMIT] = "submit", [PROF_PREPARE] = "prep", [PROF_WAIT] = "wait",
    [PROF_RASTER] = "rast", [PROF_BLIT] = "blit",     [PROF_VSYNC] = "vsync",
};

static int64_t s_open[PROF_COUNT];  // when the current begin() happened
static int64_t s_acc[PROF_COUNT];   // total inside this phase, this period
static int     s_frames;

void prof_begin(prof_phase_t p) {
    if ((unsigned)p < PROF_COUNT) s_open[p] = esp_timer_get_time();
}

void prof_end(prof_phase_t p) {
    if ((unsigned)p < PROF_COUNT) s_acc[p] += esp_timer_get_time() - s_open[p];
}

void prof_add(prof_phase_t p, int64_t us) {
    if ((unsigned)p < PROF_COUNT) s_acc[p] += us;
}

void prof_frame(void) {
    s_frames++;
}

void prof_reset(void) {
    for (int i = 0; i < PROF_COUNT; i++) s_acc[i] = 0;
    s_frames = 0;
}

int prof_snapshot(float ms[PROF_COUNT]) {
    if (s_frames == 0) return 0;
    float const inv = 1.0f / (1000.0f * (float)s_frames);
    for (int i = 0; i < PROF_COUNT; i++) ms[i] = (float)s_acc[i] * inv;
    return s_frames;
}

char const* prof_name(prof_phase_t p) {
    return ((unsigned)p < PROF_COUNT) ? NAMES[p] : "?";
}

bool prof_flush(char* out, size_t n, float frame_ms) {
    if (out == NULL || n == 0) return false;
    if (s_frames == 0) return false;

    float const inv = 1.0f / (1000.0f * (float)s_frames);  // us total -> ms per frame
    float       sum = 0.0f;
    size_t      w   = 0;

    for (int i = 0; i < PROF_COUNT && w < n; i++) {
        float const ms  = (float)s_acc[i] * inv;
        sum            += ms;
        int const c     = snprintf(out + w, n - w, "%s%s %.2f", (i ? " " : ""), NAMES[i], (double)ms);
        if (c < 0) break;
        w += (size_t)c;
    }

    // Everything no phase claimed: the engine's input pump, on_update
    // and the loop itself. Should stay small; if it grows, something
    // outside the named phases is eating the frame.
    if (w < n) {
        snprintf(out + w, n - w, "  rest %.2f", (double)(frame_ms - sum));
    }

    prof_reset();
    return true;
}
