#pragma once
// =====================================================================
//  Test kit  --  debug screen capture
// ---------------------------------------------------------------------
//  Writes the finished frame to the SD card as a PNG. A development
//  tool: some rendering faults -- a stray line, a seam, a sliver of
//  geometry that is only wrong for two frames -- are far easier to
//  diagnose from a still than from a description of one.
//
//  It stops the world while it works, which is the point. Capturing a
//  frame that is still being drawn would produce exactly the kind of
//  half-finished image that sends a debugging session down a false
//  trail, so the capture happens at the very end of the frame and simply
//  blocks. The engine clamps the next frame's dt (SE_FRAME_DT_MAX), so
//  the turntable steps by a tenth of a second rather than jumping by the
//  whole stall. Ported unchanged from Stunt Racer.
//
//  DEFLATE STORED BLOCKS, deliberately. zlib and miniz are both linkable
//  here, but every compressing path wants either a ~130 KB compressor
//  state or a whole-image output buffer, allocated through a malloc that
//  lands in scarce INTERNAL RAM. A stored stream needs one 64 KB PSRAM
//  buffer and produces a file about 1.1 MB -- which costs a second of SD
//  write and nothing that matters. A debug tool that fails when memory
//  is tight fails exactly when it is needed.
// =====================================================================

#include <stdbool.h>
#include "pax_gfx.h"

// Capture `fb` to the next free <SCREENSHOT_DIR>/shotNNN.png (see
// screenshot.c; the app sets that directory). Returns
// false, with the reason logged, if the card is missing or the write
// fails. A failed write leaves its truncated file on the card -- this
// build's libc exports neither remove() nor unlink() -- so the error
// names the file and says to delete it.
bool screenshot_capture(pax_buf_t* fb);

// Capture `fb` to exactly `path` (overwriting it). The directory must
// exist. Same failure behaviour as screenshot_capture().
bool screenshot_capture_to(pax_buf_t* fb, char const* path);
