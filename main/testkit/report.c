// =====================================================================
//  Test kit  --  test records on the debug console (see report.h)
// =====================================================================

#include "report.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t s_mutex = NULL;

// IEEE CRC32, reflected, as zlib.crc32. Table-free: records are few.
uint32_t report_crc32(void const* data, size_t len) {
    uint8_t const* p   = data;
    uint32_t       crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

void report_emit(char const* kind, char const* json) {
    static char line[REPORT_JSON_MAX + 64];
    if (s_mutex == NULL) s_mutex = xSemaphoreCreateMutex();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int const n = snprintf(line, sizeof(line), "@@" REPORT_PREFIX "-%s@@ %s @@%08" PRIx32 "@@\n", kind, json,
                           report_crc32(json, strlen(json)));
    if (n > 0) {
        // One write of the whole line: the smallest window for log output
        // from another task to interleave.
        fwrite(line, 1, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1, stdout);
        fflush(stdout);
    }
    xSemaphoreGive(s_mutex);
}

void report_emitf(char const* kind, char const* fmt, ...) {
    static char json[REPORT_JSON_MAX];
    va_list     ap;
    va_start(ap, fmt);
    vsnprintf(json, sizeof(json), fmt, ap);
    va_end(ap);
    report_emit(kind, json);
}
