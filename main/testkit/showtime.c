// =====================================================================
//  Test kit  --  the show clock (see showtime.h)
// =====================================================================

#include "showtime.h"
#include <stdbool.h>
#include "esp_timer.h"

static bool   s_fixed     = false;
static double s_step      = 1.0 / 30.0;  // seconds per frame in fixed-step mode
static double s_now       = 0.0;         // latched show time
static double s_offset    = 0.0;         // realtime: show time = wall - offset
static bool   s_offset_ok = false;       // offset taken from the first frame

// The wall clock, in seconds. The one esp_timer read the show makes.
static double wallclock(void) {
    return (double)esp_timer_get_time() * 1e-6;
}

void showtime_frame(void) {
    if (s_fixed) {
        s_now += s_step;
        return;
    }
    double const wall = wallclock();
    if (!s_offset_ok) {
        // First frame: the show starts at 0, not at the seconds the badge
        // has been up.
        s_offset    = wall - s_now;
        s_offset_ok = true;
    }
    s_now = wall - s_offset;
}

double showtime_now(void) {
    return s_now;
}

void showtime_set_realtime(void) {
    s_fixed     = false;
    s_offset    = wallclock() - s_now;
    s_offset_ok = true;
}

void showtime_set_fixed_step(float fps) {
    s_fixed = true;
    s_step  = (fps > 0.0f) ? 1.0 / (double)fps : 1.0 / 30.0;
}

void showtime_set(double t) {
    s_now = t;
    if (!s_fixed) {
        s_offset    = wallclock() - t;
        s_offset_ok = true;
    }
}

void showtime_exclude(int64_t us) {
    if (!s_fixed && us > 0) s_offset += (double)us * 1e-6;
}
