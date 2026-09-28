// =====================================================================
//  Test kit  --  debug-console command listener (see debugcon.h)
// =====================================================================

#include "debugcon.h"
#include <string.h>
// The build id, if the app generates one (see README): it is what lets
// the host refuse to believe results from a stale build on the badge.
// Without it the records still come, saying "unknown".
#if defined(__has_include)
#if __has_include("app_version.h")
#include "app_version.h"
#endif
#endif
#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif
#ifndef APP_BUILD_TIME
#define APP_BUILD_TIME "unknown"
#endif
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "report.h"
// The engine, if this app has one: its version goes into every identity
// record, which is how a test result records what it ran against.
#ifdef TESTKIT_NO_ENGINE
#define TESTKIT_ENGINE_VERSION "-"
#else
#include "synthengine3d.h"
#define TESTKIT_ENGINE_VERSION se_version_string()
#endif

static char const TAG[] = "debugcon";

#define READ_CHUNK    64
#define BANNER_PERIOD pdMS_TO_TICKS(2000)
#define QUEUE_WAIT    pdMS_TO_TICKS(100)

typedef struct {
    char line[DEBUGCON_LINE_MAX];
} cmd_t;

static QueueHandle_t              s_queue;
static volatile bool              s_busy;
static debugcon_identity_t const* s_id;

// The host matches "git" against the build it expects, so a stale app on
// the badge is caught before its results are believed; "scene" is what
// the app calls its current state.
void debugcon_hello(char const* kind) {
    char const* const app   = (s_id && s_id->app) ? s_id->app : "app";
    char const* const state = (s_id && s_id->state) ? s_id->state() : "";
    report_emitf(kind,
                 "{\"t\":\"%s\",\"app\":\"%s\",\"git\":\"%s\",\"built\":\"%s\",\"engine\":\"%s\","
                 "\"scene\":\"%s\",\"free_int\":%u,\"free_psram\":%u}",
                 kind, app, APP_GIT_HASH, APP_BUILD_TIME, TESTKIT_ENGINE_VERSION, state ? state : "",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void handle_line(char const* line) {
    if (line[0] == '\0') return;
    if (strcmp(line, "PING") == 0) {
        debugcon_hello("PONG");
        return;
    }
    if (strncmp(line, "RUN ", 4) == 0 || strcmp(line, "EXIT") == 0 || strcmp(line, "BADGELINK") == 0) {
        // Stop the banner right away, before the main loop picks it up.
        s_busy = true;
        cmd_t cmd;
        strlcpy(cmd.line, line, sizeof(cmd.line));
        // Never block the listener on a main loop that is not draining
        // the queue: drop the command instead, say so, and let the
        // banner come back, which tells the host we are still idle.
        if (xQueueSend(s_queue, &cmd, QUEUE_WAIT) != pdTRUE) {
            ESP_LOGE(TAG, "command queue full, dropped: %s", line);
            s_busy = false;
        }
        return;
    }
    ESP_LOGW(TAG, "unknown command: %s", line);
}

static void debugcon_task(void* arg) {
    (void)arg;
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 1024,
    };
    esp_err_t const res = usb_serial_jtag_driver_install(&config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "USB-serial/JTAG driver install failed: %s -- no debug commands", esp_err_to_name(res));
        vTaskDelete(NULL);
        return;
    }

    char   line[DEBUGCON_LINE_MAX];
    size_t len = 0;
    for (;;) {
        uint8_t   buf[READ_CHUNK];
        int const n = usb_serial_jtag_read_bytes(buf, sizeof(buf), BANNER_PERIOD);
        if (n <= 0) {
            if (!s_busy) debugcon_hello("READY");
            continue;
        }
        for (int i = 0; i < n; i++) {
            char const c = (char)buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            } else {
                len = 0;  // overlong line: drop it, resync at the next newline
            }
        }
    }
}

void debugcon_start(debugcon_identity_t const* id) {
    s_id    = id;
    s_queue = xQueueCreate(4, sizeof(cmd_t));
    xTaskCreate(debugcon_task, "debugcon", 4096, NULL, 4, NULL);
}

bool debugcon_poll(char out[DEBUGCON_LINE_MAX]) {
    cmd_t cmd;
    if (s_queue == NULL || xQueueReceive(s_queue, &cmd, 0) != pdTRUE) return false;
    strlcpy(out, cmd.line, DEBUGCON_LINE_MAX);
    return true;
}

void debugcon_set_busy(bool busy) {
    s_busy = busy;
}
