// =====================================================================
//  Test kit  --  debug screen capture (see screenshot.h)
// =====================================================================

#include "screenshot.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static char const TAG[] = "shot";

// Where auto-numbered shots land. An app sets its own in CMakeLists:
// add_compile_definitions(SCREENSHOT_DIR="/sd/myapp"). Created if it is
// not there; screenshot_capture_to() ignores it and takes a full path.
#ifndef SCREENSHOT_DIR
#define SCREENSHOT_DIR "/sd/testkit"
#endif
#define SHOT_DIR    SCREENSHOT_DIR
#define SHOT_MAX    1000
#define BLOCK_LIMIT 60000u  // payload per stored block; the format allows 65535

// zlib's, from the SDK. Declared here rather than pulling in <zlib.h>,
// which the graceloader include set does not carry.
extern unsigned long crc32(unsigned long crc, unsigned char const* buf, unsigned len);
extern unsigned long adler32(unsigned long adler, unsigned char const* buf, unsigned len);

// ---- PNG chunk plumbing ---------------------------------------------

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// One PNG chunk: length, type, payload, CRC over type+payload.
static bool write_chunk(FILE* f, char const type[4], uint8_t const* data, uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return false;
    if (len > 0 && fwrite(data, 1, len, f) != len) return false;

    unsigned long crc = crc32(0, (unsigned char const*)type, 4);
    if (len > 0) crc = crc32(crc, data, len);

    uint8_t tail[4];
    put_be32(tail, (uint32_t)crc);
    return fwrite(tail, 1, 4, f) == 4;
}

// One IDAT carrying a single DEFLATE stored block. `first` prefixes the
// two-byte zlib header, which must open the concatenated IDAT stream.
static bool write_stored_idat(FILE* f, uint8_t* scratch, uint8_t const* payload, uint32_t len, bool first) {
    uint32_t n = 0;
    if (first) {
        scratch[n++] = 0x78;  // CM = deflate, CINFO = 32K window
        scratch[n++] = 0x01;  // no preset dict, check bits make it a multiple of 31
    }
    scratch[n++] = 0x00;  // BFINAL = 0, BTYPE = 00 (stored), then pad to a byte
    scratch[n++] = (uint8_t)(len & 0xFF);
    scratch[n++] = (uint8_t)(len >> 8);
    scratch[n++] = (uint8_t)(~len & 0xFF);
    scratch[n++] = (uint8_t)((~len >> 8) & 0xFF);
    memmove(scratch + n, payload, len);
    return write_chunk(f, "IDAT", scratch, n + len);
}

// ---- capture ---------------------------------------------------------

static bool next_path(char* out, size_t out_len) {
    mkdir("/sd", 0777);
    mkdir(SHOT_DIR, 0777);
    for (int i = 0; i < SHOT_MAX; i++) {
        snprintf(out, out_len, SHOT_DIR "/shot%03d.png", i);
        struct stat st;
        if (stat(out, &st) != 0) return true;
    }
    return false;
}

bool screenshot_capture(pax_buf_t* fb) {
    char path[64];
    if (!next_path(path, sizeof(path))) {
        ESP_LOGE(TAG, "no free filename in " SHOT_DIR);
        return false;
    }
    return screenshot_capture_to(fb, path);
}

bool screenshot_capture_to(pax_buf_t* fb, char const* path) {
    if (fb == NULL || path == NULL) return false;

    int const w = (int)pax_buf_get_width(fb);
    int const h = (int)pax_buf_get_height(fb);
    if (w <= 0 || h <= 0) return false;

    uint32_t const row_len = 1u + (uint32_t)w * 3u;  // filter byte + RGB triples
    if (row_len > BLOCK_LIMIT) {
        ESP_LOGE(TAG, "screen too wide for one stored block (%u bytes/row)", (unsigned)row_len);
        return false;
    }

    // One buffer holds a batch of rows; the same allocation is reused as
    // the chunk scratch, so it carries the 7-byte block preamble too.
    uint32_t const rows_per_block = BLOCK_LIMIT / row_len;
    uint32_t const batch_len      = rows_per_block * row_len;
    uint8_t* const buf            = heap_caps_malloc(batch_len, MALLOC_CAP_SPIRAM);
    uint8_t* const scratch        = heap_caps_malloc(batch_len + 8u, MALLOC_CAP_SPIRAM);
    if (buf == NULL || scratch == NULL) {
        ESP_LOGE(TAG, "out of PSRAM for a %ux%u capture", (unsigned)w, (unsigned)h);
        heap_caps_free(buf);
        heap_caps_free(scratch);
        return false;
    }

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "cannot open %s -- is the SD card in?", path);
        heap_caps_free(buf);
        heap_caps_free(scratch);
        return false;
    }

    bool ok = true;
    ok      = ok && fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) == 8;

    uint8_t ihdr[13];
    put_be32(ihdr + 0, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8]  = 8;  // 8 bits per channel
    ihdr[9]  = 2;  // truecolour RGB
    ihdr[10] = 0;  // deflate
    ihdr[11] = 0;  // adaptive filtering
    ihdr[12] = 0;  // no interlace
    ok       = ok && write_chunk(f, "IHDR", ihdr, sizeof(ihdr));

    unsigned long adler = adler32(0, NULL, 0);
    bool          first = true;
    uint32_t      fill  = 0;

    for (int y = 0; ok && y < h; y++) {
        // Logical coordinates: pax_get_pixel undoes the panel rotation,
        // so the file comes out the way the screen looks rather than the
        // way the framebuffer is laid out.
        uint8_t* p = buf + fill;
        *p++       = 0;  // filter type 0 (None)
        for (int x = 0; x < w; x++) {
            pax_col_t const c = pax_get_pixel(fb, x, y);
            *p++              = (uint8_t)(c >> 16);
            *p++              = (uint8_t)(c >> 8);
            *p++              = (uint8_t)c;
        }
        fill += row_len;

        if (fill + row_len > batch_len || y == h - 1) {
            adler = adler32(adler, buf, fill);
            ok    = ok && write_stored_idat(f, scratch, buf, fill, first);
            first = false;
            fill  = 0;
        }
    }

    // The final (empty) stored block closes the deflate stream, then the
    // Adler-32 of everything that went into it closes the zlib stream.
    if (ok) {
        uint8_t tail[9] = {0x01, 0x00, 0x00, 0xFF, 0xFF, 0, 0, 0, 0};
        put_be32(tail + 5, (uint32_t)adler);
        ok = write_chunk(f, "IDAT", tail, sizeof(tail));
    }
    ok = ok && write_chunk(f, "IEND", NULL, 0);

    if (fclose(f) != 0) ok = false;
    heap_caps_free(buf);
    heap_caps_free(scratch);

    if (ok) {
        ESP_LOGI(TAG, "wrote %s (%dx%d)", path, w, h);
    } else {
        // The partial file is left behind: this build's libc exports no
        // remove() or unlink(). Say so plainly rather than leaving a
        // truncated PNG to be puzzled over later.
        ESP_LOGE(TAG, "failed writing %s -- card full or removed? DELETE THIS FILE, it is truncated", path);
    }
    return ok;
}
