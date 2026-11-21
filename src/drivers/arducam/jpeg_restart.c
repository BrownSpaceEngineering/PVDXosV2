/**
 * jpeg_restart.c
 *
 * Lossless insertion of JPEG restart markers (see jpeg_restart.h). Section and table numbers refer to ITU-T T.81, the
 * JPEG standard.
 *
 * Created: November 9, 2026
 */

#include "jpeg_restart.h"

#include <string.h>

#define MARKER_SOF0 0xC0
#define MARKER_SOF1 0xC1
#define MARKER_DHT 0xC4
#define MARKER_RST0 0xD0
#define MARKER_SOI 0xD8
#define MARKER_EOI 0xD9
#define MARKER_SOS 0xDA
#define MARKER_DRI 0xDD

#define MAX_COMPONENTS 4
#define MAX_BLOCKS_PER_MCU 10 // T.81 B.2.3
#define SEGMENT_BUF_SIZE 1100 // Enough for a DHT segment holding all four baseline tables
#define IO_BUF_SIZE 512
#define LOOKAHEAD_BITS 8   // Codes up to this long are decoded with one table lookup

typedef struct {
    bool present;
    uint8_t vals[256];
    // Decoding, T.81 F.2.2.3
    int32_t mincode[17];
    int32_t maxcode[17]; // -1 when no codes have that length
    uint8_t valptr[17];
    // Encoding, T.81 C.2
    uint16_t code[256];
    uint8_t size[256]; // 0 when the symbol has no code
    uint16_t lookup[1 << LOOKAHEAD_BITS]; // (length << 8) | symbol for codes up to LOOKAHEAD_BITS long, else 0
} huff_table_t;

typedef struct {
    uint8_t id;
    uint8_t h, v;
    uint8_t dc_table, ac_table;
} component_t;

static struct {
    jpeg_restart_read_fn read;
    void *read_ctx;
    jpeg_restart_write_fn write;
    void *write_ctx;

    uint8_t in_buf[IO_BUF_SIZE];
    size_t in_len, in_pos;
    uint8_t out_buf[IO_BUF_SIZE];
    size_t out_len, out_total;
    bool write_failed;

    // Entropy-coded data. Input bits are read ahead up to the next marker, which in_marker then holds.
    uint32_t in_bits, out_bits;
    int in_bit_count, out_bit_count;
    bool at_marker;
    uint8_t in_marker; // 0 at the end of the input
    bool bad_input;

    uint8_t segment[SEGMENT_BUF_SIZE];
    huff_table_t dc[2], ac[2];
    component_t components[MAX_COMPONENTS];
    int num_components;
    uint16_t width, height;
    uint16_t in_restart_interval;
} s;

/* ---------- Byte I/O ---------- */

static bool get_byte(uint8_t *b) {
    if (s.in_pos == s.in_len) {
        s.in_len = s.read(s.read_ctx, s.in_buf, sizeof(s.in_buf));
        s.in_pos = 0;
        if (s.in_len == 0) {
            return false;
        }
    }
    *b = s.in_buf[s.in_pos++];
    return true;
}

static void flush_output(void) {
    if (s.out_len && !s.write_failed && !s.write(s.write_ctx, s.out_buf, s.out_len)) {
        s.write_failed = true;
    }
    s.out_len = 0;
}

static void put_byte(uint8_t b) {
    s.out_buf[s.out_len++] = b;
    s.out_total++;
    if (s.out_len == sizeof(s.out_buf)) {
        flush_output();
    }
}

static void put_marker(uint8_t marker) {
    put_byte(0xFF);
    put_byte(marker);
}

/* ---------- Entropy-coded bits ---------- */

static uint32_t low_bits(uint32_t v, int n) {
    return v & ((1u << n) - 1);
}

// Reads ahead to at least 25 bits, unstuffing 0xFF 0x00, unless a marker or the end of the input comes first
static void fill_bits(void) {
    while (s.in_bit_count <= 24 && !s.at_marker) {
        uint8_t b = 0;
        if (!get_byte(&b)) {
            s.at_marker = true;
            s.in_marker = 0;
            break;
        }
        if (b == 0xFF) {
            uint8_t next = 0;
            if (!get_byte(&next) || next != 0x00) {
                s.at_marker = true;
                s.in_marker = next;
                break;
            }
        }
        s.in_bits = (s.in_bits << 8) | b;
        s.in_bit_count += 8;
    }
}

// Past a marker, the bits read as zeros (and taking them sets bad_input)
static uint32_t peek_bits(int n) {
    if (s.in_bit_count < n) {
        fill_bits();
        if (s.in_bit_count < n) {
            return low_bits(s.in_bits, s.in_bit_count) << (n - s.in_bit_count);
        }
    }
    return low_bits(s.in_bits >> (s.in_bit_count - n), n);
}

static void skip_bits(int n) {
    if (n > s.in_bit_count) {
        s.bad_input = true;
        s.in_bit_count = 0;
    } else {
        s.in_bit_count -= n;
    }
}

static uint32_t get_bits(int n) {
    const uint32_t v = peek_bits(n);
    skip_bits(n);
    return v;
}

static void put_bits(uint32_t bits, int n) {
    s.out_bits = (s.out_bits << n) | (bits & ((1u << n) - 1));
    s.out_bit_count += n;
    while (s.out_bit_count >= 8) {
        s.out_bit_count -= 8;
        const uint8_t b = (uint8_t)(s.out_bits >> s.out_bit_count);
        put_byte(b);
        if (b == 0xFF) {
            put_byte(0x00);
        }
    }
}

// Pads the output to a byte boundary with 1 bits, as before a marker (T.81 F.1.2.3)
static void pad_output(void) {
    if (s.out_bit_count) {
        put_bits(0x7F, 8 - s.out_bit_count);
    }
}

static int decode_symbol(const huff_table_t *t) {
    const uint16_t entry = t->lookup[peek_bits(LOOKAHEAD_BITS)];
    if (entry) {
        skip_bits(entry >> 8);
        return entry & 0xFF;
    }
    // A longer code (T.81 F.2.2.3)
    const uint32_t bits = peek_bits(16);
    for (int len = LOOKAHEAD_BITS + 1; len <= 16; len++) {
        const int32_t code = (int32_t)(bits >> (16 - len));
        if (t->maxcode[len] >= 0 && code <= t->maxcode[len]) {
            skip_bits(len);
            return t->vals[t->valptr[len] + code - t->mincode[len]];
        }
    }
    s.bad_input = true;
    return 0;
}

static bool put_symbol(const huff_table_t *t, int symbol) {
    if (!t->size[symbol]) {
        return false;
    }
    put_bits(t->code[symbol], t->size[symbol]);
    return true;
}

/* ---------- Header ---------- */

// MCUs per row and per column. A single-component scan is not interleaved, so each MCU is one block (T.81 A.2.2).
static void mcu_counts(uint32_t *mcus_x, uint32_t *mcus_y) {
    uint32_t h_max = 1, v_max = 1;
    if (s.num_components > 1) {
        for (int i = 0; i < s.num_components; i++) {
            h_max = s.components[i].h > h_max ? s.components[i].h : h_max;
            v_max = s.components[i].v > v_max ? s.components[i].v : v_max;
        }
    }
    *mcus_x = (s.width + 8 * h_max - 1) / (8 * h_max);
    *mcus_y = (s.height + 8 * v_max - 1) / (8 * v_max);
}

static bool read_u16(uint16_t *v) {
    uint8_t hi = 0, lo = 0;
    if (!get_byte(&hi) || !get_byte(&lo)) {
        return false;
    }
    *v = (uint16_t)((hi << 8) | lo);
    return true;
}

static bool read_segment(size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!get_byte(&s.segment[i])) {
            return false;
        }
    }
    return true;
}

static void put_segment(uint8_t marker, size_t len) {
    put_marker(marker);
    put_byte((uint8_t)((len + 2) >> 8));
    put_byte((uint8_t)(len + 2));
    for (size_t i = 0; i < len; i++) {
        put_byte(s.segment[i]);
    }
}

// T.81 B.2.4.2 and Annex C
static jpeg_restart_result_t parse_dht(size_t len) {
    size_t pos = 0;
    while (pos < len) {
        const uint8_t tc = s.segment[pos] >> 4, th = s.segment[pos] & 0x0F;
        if (tc > 1 || pos + 17 > len) {
            return JPEG_RESTART_BAD_INPUT;
        }
        if (th > 1) {
            return JPEG_RESTART_UNSUPPORTED; // Baseline uses tables 0 and 1 only
        }
        huff_table_t *t = tc ? &s.ac[th] : &s.dc[th];
        memset(t, 0, sizeof(*t));
        const uint8_t *bits = &s.segment[pos + 1];
        size_t count = 0;
        for (int len_i = 0; len_i < 16; len_i++) {
            count += bits[len_i];
        }
        pos += 17;
        if (count > 256 || pos + count > len) {
            return JPEG_RESTART_BAD_INPUT;
        }
        memcpy(t->vals, &s.segment[pos], count);
        pos += count;

        int32_t code = 0;
        size_t k = 0;
        for (int code_len = 1; code_len <= 16; code_len++) {
            const uint8_t n = bits[code_len - 1];
            t->maxcode[code_len] = -1;
            if (n) {
                t->valptr[code_len] = (uint8_t)k;
                t->mincode[code_len] = code;
                for (uint8_t i = 0; i < n; i++, k++, code++) {
                    t->code[t->vals[k]] = (uint16_t)code;
                    t->size[t->vals[k]] = (uint8_t)code_len;
                    if (code_len <= LOOKAHEAD_BITS) {
                        // Every lookahead that starts with this code
                        const int shift = LOOKAHEAD_BITS - code_len;
                        for (int32_t ahead = code << shift; ahead < (code + 1) << shift; ahead++) {
                            t->lookup[ahead] = (uint16_t)((code_len << 8) | t->vals[k]);
                        }
                    }
                }
                t->maxcode[code_len] = code - 1;
            }
            if (code > (1 << code_len)) {
                return JPEG_RESTART_BAD_INPUT;
            }
            code <<= 1;
        }
        t->present = true;
    }
    return JPEG_RESTART_OK;
}

// T.81 B.2.2
static jpeg_restart_result_t parse_sof(size_t len) {
    if (len < 6 || s.segment[0] != 8) {
        return len < 6 ? JPEG_RESTART_BAD_INPUT : JPEG_RESTART_UNSUPPORTED;
    }
    s.height = (uint16_t)((s.segment[1] << 8) | s.segment[2]);
    s.width = (uint16_t)((s.segment[3] << 8) | s.segment[4]);
    s.num_components = s.segment[5];
    if (s.num_components < 1 || s.num_components > MAX_COMPONENTS || len != 6 + 3u * s.num_components || !s.width ||
        !s.height) {
        return JPEG_RESTART_BAD_INPUT;
    }
    for (int i = 0; i < s.num_components; i++) {
        component_t *c = &s.components[i];
        c->id = s.segment[6 + 3 * i];
        c->h = s.segment[7 + 3 * i] >> 4;
        c->v = s.segment[7 + 3 * i] & 0x0F;
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) {
            return JPEG_RESTART_BAD_INPUT;
        }
    }
    return JPEG_RESTART_OK;
}

// T.81 B.2.3. Only a single scan holding every component, as the OV2640 produces.
static jpeg_restart_result_t parse_sos(size_t len) {
    if (!s.num_components || len < 1 || len != 4 + 2u * s.segment[0]) {
        return JPEG_RESTART_BAD_INPUT;
    }
    if (s.segment[0] != s.num_components) {
        return JPEG_RESTART_UNSUPPORTED;
    }
    for (int i = 0; i < s.num_components; i++) {
        if (s.segment[1 + 2 * i] != s.components[i].id) {
            return JPEG_RESTART_UNSUPPORTED;
        }
        const uint8_t td = s.segment[2 + 2 * i] >> 4, ta = s.segment[2 + 2 * i] & 0x0F;
        if (td > 1 || ta > 1 || !s.dc[td].present || !s.ac[ta].present) {
            return JPEG_RESTART_BAD_INPUT;
        }
        s.components[i].dc_table = td;
        s.components[i].ac_table = ta;
    }
    const uint8_t *spectral = &s.segment[1 + 2 * s.num_components];
    if (spectral[0] != 0 || spectral[1] != 63 || spectral[2] != 0) {
        return JPEG_RESTART_UNSUPPORTED;
    }
    return JPEG_RESTART_OK;
}

// Copies everything up to and including SOS, dropping any DRI and adding one for restart_interval after the frame
// header. Returns the restart interval to use: one MCU row.
static jpeg_restart_result_t copy_header(uint16_t *restart_interval) {
    uint8_t b = 0, marker = 0;
    if (!get_byte(&b) || b != 0xFF || !get_byte(&marker) || marker != MARKER_SOI) {
        return JPEG_RESTART_BAD_INPUT;
    }
    put_marker(MARKER_SOI);

    while (true) {
        if (!get_byte(&b) || b != 0xFF) {
            return JPEG_RESTART_BAD_INPUT;
        }
        do {
            if (!get_byte(&marker)) {
                return JPEG_RESTART_BAD_INPUT;
            }
        } while (marker == 0xFF); // Fill bytes
        if (marker == MARKER_EOI || marker == MARKER_SOI || (marker >= MARKER_RST0 && marker <= MARKER_RST0 + 7) ||
            marker == 0x00 || marker == 0x01) {
            return JPEG_RESTART_BAD_INPUT;
        }
        uint16_t seg_len = 0;
        if (!read_u16(&seg_len) || seg_len < 2) {
            return JPEG_RESTART_BAD_INPUT;
        }
        const size_t len = seg_len - 2u;

        const bool parsed = marker == MARKER_DHT || marker == MARKER_SOF0 || marker == MARKER_SOF1 ||
                            marker == MARKER_DRI || marker == MARKER_SOS;
        if (!parsed) {
            if ((marker & 0xF0) == 0xC0 && marker != 0xC8 && marker != 0xCC) {
                return JPEG_RESTART_UNSUPPORTED; // Another SOFn: progressive, lossless or arithmetic coding
            }
            // APPn, COM, DQT and the like: copy them through without buffering, since APPn can be long
            put_marker(marker);
            put_byte((uint8_t)(seg_len >> 8));
            put_byte((uint8_t)seg_len);
            for (size_t i = 0; i < len; i++) {
                if (!get_byte(&b)) {
                    return JPEG_RESTART_BAD_INPUT;
                }
                put_byte(b);
            }
            continue;
        }

        if (len > sizeof(s.segment)) {
            return JPEG_RESTART_UNSUPPORTED;
        }
        if (!read_segment(len)) {
            return JPEG_RESTART_BAD_INPUT;
        }
        jpeg_restart_result_t result = JPEG_RESTART_OK;
        switch (marker) {
            case MARKER_DHT:
                result = parse_dht(len);
                put_segment(marker, len);
                break;
            case MARKER_SOF0:
            case MARKER_SOF1:
                result = parse_sof(len);
                put_segment(marker, len);
                break;
            case MARKER_DRI:
                // Replaced by ours, written just before SOS
                if (len != 2) {
                    return JPEG_RESTART_BAD_INPUT;
                }
                s.in_restart_interval = (uint16_t)((s.segment[0] << 8) | s.segment[1]);
                break;
            case MARKER_SOS: {
                result = parse_sos(len);
                if (result != JPEG_RESTART_OK) {
                    return result;
                }
                uint32_t mcus_x = 0, mcus_y = 0;
                mcu_counts(&mcus_x, &mcus_y);
                *restart_interval = (uint16_t)mcus_x;
                put_marker(MARKER_DRI);
                put_byte(0x00);
                put_byte(0x04);
                put_byte((uint8_t)(*restart_interval >> 8));
                put_byte((uint8_t)*restart_interval);
                put_segment(marker, len);
                return JPEG_RESTART_OK;
            }
            default:
                break;
        }
        if (result != JPEG_RESTART_OK) {
            return result;
        }
    }
}

/* ---------- Scan ---------- */

static jpeg_restart_result_t copy_scan(uint16_t restart_interval) {
    uint32_t mcus_x = 0, mcus_y = 0;
    mcu_counts(&mcus_x, &mcus_y);
    const uint32_t total_mcus = mcus_x * mcus_y;
    if (s.num_components == 1) {
        s.components[0].h = s.components[0].v = 1; // Not interleaved: one block per MCU
    }
    int blocks_per_mcu = 0;
    for (int i = 0; i < s.num_components; i++) {
        blocks_per_mcu += s.components[i].h * s.components[i].v;
    }
    if (blocks_per_mcu > MAX_BLOCKS_PER_MCU) {
        return JPEG_RESTART_BAD_INPUT;
    }

    int in_dc[MAX_COMPONENTS] = {0}, out_dc[MAX_COMPONENTS] = {0};
    uint8_t next_rst = 0;
    for (uint32_t mcu = 0; mcu < total_mcus; mcu++) {
        if (mcu && s.in_restart_interval && mcu % s.in_restart_interval == 0) {
            // The input's own restart marker: only padding bits may come before it. Skip them and the marker, and
            // reset the input's predictors.
            if (s.in_bit_count >= 8) {
                return JPEG_RESTART_BAD_INPUT;
            }
            s.in_bit_count = 0;
            fill_bits();
            if (!s.at_marker || s.in_marker < MARKER_RST0 || s.in_marker > MARKER_RST0 + 7) {
                return JPEG_RESTART_BAD_INPUT;
            }
            s.at_marker = false;
            memset(in_dc, 0, sizeof(in_dc));
        }
        if (mcu && mcu % restart_interval == 0) {
            pad_output();
            put_marker((uint8_t)(MARKER_RST0 + next_rst));
            next_rst = (next_rst + 1) & 7;
            memset(out_dc, 0, sizeof(out_dc));
        }

        for (int c = 0; c < s.num_components; c++) {
            const huff_table_t *dc_table = &s.dc[s.components[c].dc_table];
            const huff_table_t *ac_table = &s.ac[s.components[c].ac_table];
            for (int block = 0; block < s.components[c].h * s.components[c].v; block++) {
                // DC: decode the difference, then re-encode it against the output's predictor (T.81 F.1.2.1)
                const int category = decode_symbol(dc_table);
                if (category > 11) {
                    return JPEG_RESTART_BAD_INPUT;
                }
                int diff = 0;
                if (category) {
                    diff = (int)get_bits(category);
                    if (diff < (1 << (category - 1))) {
                        diff -= (1 << category) - 1;
                    }
                }
                in_dc[c] += diff;
                int out_diff = in_dc[c] - out_dc[c];
                out_dc[c] = in_dc[c];
                int out_category = 0;
                for (int magnitude = out_diff < 0 ? -out_diff : out_diff; magnitude; magnitude >>= 1) {
                    out_category++;
                }
                if (out_category > 11 || !put_symbol(dc_table, out_category)) {
                    // The camera's DC table has no code for this difference
                    return JPEG_RESTART_UNSUPPORTED;
                }
                if (out_category) {
                    put_bits((uint32_t)(out_diff < 0 ? out_diff - 1 : out_diff), out_category);
                }

                // AC: copied as decoded (T.81 F.1.2.2)
                for (int k = 1; k < 64;) {
                    const int rs = decode_symbol(ac_table);
                    const int run = rs >> 4, size = rs & 0x0F;
                    put_symbol(ac_table, rs); // Decoded from this table, so it has a code
                    if (size) {
                        put_bits(get_bits(size), size);
                        k += run + 1;
                    } else if (run == 15) {
                        k += 16;
                    } else {
                        break; // EOB
                    }
                    if (k > 64) {
                        return JPEG_RESTART_BAD_INPUT;
                    }
                }
                if (s.bad_input) {
                    return JPEG_RESTART_BAD_INPUT;
                }
            }
        }
        if (s.write_failed) {
            return JPEG_RESTART_WRITE_FAILED;
        }
    }
    pad_output();
    put_marker(MARKER_EOI);
    return JPEG_RESTART_OK;
}

/**
 * \fn jpeg_add_restart_markers
 *
 * \brief Copies a JPEG from read to write, adding a restart marker after every MCU row. Anything after the scan
 *      (such as the padding after the OV2640's EOI) is dropped. Not reentrant.
 *
 * \param p_out_len set to the number of bytes written, whatever the result
 *
 * \returns `jpeg_restart_result_t`, JPEG_RESTART_OK if the whole image was written
 */
jpeg_restart_result_t jpeg_add_restart_markers(jpeg_restart_read_fn read, void *read_ctx, jpeg_restart_write_fn write,
                                               void *write_ctx, size_t *p_out_len) {
    memset(&s, 0, sizeof(s));
    s.read = read;
    s.read_ctx = read_ctx;
    s.write = write;
    s.write_ctx = write_ctx;

    uint16_t restart_interval = 0;
    jpeg_restart_result_t result = copy_header(&restart_interval);
    if (result == JPEG_RESTART_OK) {
        result = copy_scan(restart_interval);
    }
    flush_output();
    if (result == JPEG_RESTART_OK && s.write_failed) {
        result = JPEG_RESTART_WRITE_FAILED;
    }
    *p_out_len = s.out_total;
    return result;
}
