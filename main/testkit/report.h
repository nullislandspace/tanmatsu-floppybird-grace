#pragma once
// =====================================================================
//  Test kit  --  machine-readable test records on the debug console
// ---------------------------------------------------------------------
//  After tanmatsu-idf6tests' report.c. Each record is one line:
//
//      @@<PREFIX>-<KIND>@@ <compact json> @@<crc32 hex8>@@
//
//  PREFIX is "SR" unless the app defines REPORT_PREFIX; whatever it is,
//  tools/testrun.py must be told the same (--prefix).
//
//  The CRC covers the JSON and matches Python's zlib.crc32, so
//  tools/testrun.py can drop a line that some other task's log output
//  landed in the middle of. Records never go through ESP_LOG.
// =====================================================================

#include <stddef.h>
#include <stdint.h>

#define REPORT_JSON_MAX 1536

// The record prefix, two or three letters, app-wide. Override it in the
// app's CMakeLists (add_compile_definitions(REPORT_PREFIX="MG")) if two
// apps' logs ever end up in one file.
#ifndef REPORT_PREFIX
#define REPORT_PREFIX "SR"
#endif

uint32_t report_crc32(void const* data, size_t len);

void report_emit(char const* kind, char const* json);
void report_emitf(char const* kind, char const* fmt, ...) __attribute__((format(printf, 2, 3)));
