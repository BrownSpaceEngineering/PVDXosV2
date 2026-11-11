/**
 * jpeg_restart.h
 *
 * Lossless insertion of JPEG restart markers, like `jpegtran -restart`. The OV2640 doesn't put restart markers in
 * its JPEGs, so one lost downlink packet would corrupt the rest of the image. With a restart marker after every MCU
 * row, a lost packet only costs the rows it falls in: the decoder picks up again at the next marker.
 *
 * Only the DC coefficients are re-encoded (their predictors restart at each marker); everything else is copied bit
 * for bit, so the decoded image is unchanged. Supports baseline (SOF0/SOF1) JPEGs with one interleaved scan, with or
 * without restart markers of their own.
 */

#ifndef JPEG_RESTART_H
#define JPEG_RESTART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Supplies the next input bytes: writes up to cap bytes to buf and returns how many, 0 at the end of the input
typedef size_t (*jpeg_restart_read_fn)(void *ctx, uint8_t *buf, size_t cap);
// Takes the next output bytes; returns false to abort
typedef bool (*jpeg_restart_write_fn)(void *ctx, const uint8_t *buf, size_t len);

typedef enum {
    JPEG_RESTART_OK,
    JPEG_RESTART_BAD_INPUT,   // Not a well-formed JPEG
    JPEG_RESTART_UNSUPPORTED, // A JPEG this doesn't handle, e.g. progressive or multi-scan
    JPEG_RESTART_WRITE_FAILED,
} jpeg_restart_result_t;

jpeg_restart_result_t jpeg_add_restart_markers(jpeg_restart_read_fn read, void *read_ctx, jpeg_restart_write_fn write,
                                               void *write_ctx, size_t *p_out_len);

#endif // JPEG_RESTART_H
