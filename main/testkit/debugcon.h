#pragma once
// =====================================================================
//  Test kit  --  debug-console command listener
// ---------------------------------------------------------------------
//  After tanmatsu-idf6tests' console.c (itself after tanmatsu-fonttest).
//  A task reads newline-terminated commands from the USB-serial/JTAG
//  peripheral through the DRIVER, never through stdin (mixing the two
//  loses bytes); printf/ESP_LOG output keeps its normal console path.
//
//  The driver has to be exported by graceloader: 2.4.0 and later do.
//
//  Commands:
//    PING              answered with a PONG record by the listener
//    RUN <test> [k=v]  queued for the main loop (devtest.h)
//    EXIT              queued: return to the launcher
//    BADGELINK         queued, same as EXIT. The host sends this to ask
//                      the LAUNCHER for BadgeLink mode; if the app is
//                      still running, it leaves first, so the host's
//                      next attempt reaches the launcher.
//
//  While idle it emits a READY record every 2 s, which is how the host
//  finds the app without having to send anything first. There is no idle
//  timeout: outside a test the app is a normal app and keeps running.
//
//  Normally started by devtest_start(), not by the app directly.
// =====================================================================

#include <stdbool.h>

#define DEBUGCON_LINE_MAX 256

// What every identity record (READY / PONG / HELLO) carries besides the
// build id, engine version and free heap: the app's slug, and one word
// about what it is doing -- for a reel the scene that is playing, for a
// game the state it is in. `state` may be NULL, or may return "".
typedef struct {
    char const* app;
    char const* (*state)(void);
} debugcon_identity_t;

// Start the listener task (installs the USB-serial/JTAG driver).
// `id` must point at storage that outlives the call (a static).
void debugcon_start(debugcon_identity_t const* id);

// Take the next queued command line, if any, without blocking.
bool debugcon_poll(char out[DEBUGCON_LINE_MAX]);

// The READY banner: on while idle, off while a command is being carried
// out, so a test's records are not interleaved with it.
//
// Queueing a command turns it off by itself, straight away, so nothing
// slips out between the queue and the main loop picking it up -- and a
// command that cannot be queued turns it back on. Whoever runs the
// command owns the flag from then on and clears it when it is back to
// idle (devtest.c does, at the end of a test). A runner that always
// leaves for the launcher never has to.
void debugcon_set_busy(bool busy);

// Emit the identity record (READY / PONG / HELLO share their fields).
void debugcon_hello(char const* kind);
