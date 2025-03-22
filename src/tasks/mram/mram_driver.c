// TODO: verify that MRAM starts in 1S-1S-1S transfer mode

#include "../../../ASF/driver_init.h"
#include "../../../ASF/hal/include/hal_spi_m_sync.h"
#include "../../../ASF/hal/include/hpl_spi.h"

#define massert(x) ((x) ? 0 : fatal("massert failed"));

#define MRAM_MAX_ADDRESS        8388608 // 8MiB
#define MRAM_REGION_SIZE        1000000
#define MRAM_REGION_BUF_SIZE    4096

#define MRAM_CS_LOW()           gpio_set_pin_level(0, 0)
#define MRAM_CS_HIGH()          gpio_set_pin_level(0, 1)

#define MRAM_CMD_READ           0x03
#define MRAM_CMD_WRITE_EN       0x06
#define MRAM_CMD_WRITE          0x02

void mram_write_raw(long pos, long len, const char *buf) {
    massert(0 <= pos && pos < MRAM_MAX_ADDRESS);
    massert(0 <= len && pos + len < MRAM_MAX_ADDRESS);

    // Send write enable command, otherwise write command will be ignored
    MRAM_CS_LOW();
    char tx_buf[] = {MRAM_CMD_WRITE_EN};
    massert(SPI_0.io.write(&SPI_0, tx_buf, 1) == 1);
    MRAM_CS_HIGH();

    MRAM_CS_LOW();

    // Send write command
    tx_buf[0] = MRAM_CMD_WRITE;
    massert(SPI_0.io.write(&SPI_0, tx_buf, 1) == 1);

    // Send len bytes of data to be written
    massert(SPI_0.io.write(&SPI_0, buf, len));

    MRAM_CS_HIGH();
}

void mram_read_raw(long pos, long len, char *buf) {
    massert(0 <= pos && pos < MRAM_MAX_ADDRESS);
    massert(0 <= len && pos + len < MRAM_MAX_ADDRESS);

    MRAM_CS_LOW();

    // Send read command, followed by 3-byte address in MSB-first order
    char tx_buf[] = {
        MRAM_CMD_READ,
        (pos >> 16) & 0xff,
        (pos >> 8) & 0xff,
        pos & 0xff
    };
    massert(SPI_0.io.write(&SPI_0, tx_buf, 4) == 4);

    // Read len bytes
    massert(SPI_0.io.read(&SPI_0, buf, len) == len);

    MRAM_CS_HIGH();
}

void mram_write(long pos, long len, const char *buf) {
    massert(pos < MRAM_REGION_SIZE);
    massert(pos + len < MRAM_REGION_SIZE);

    // Write a copy of buf to each region at index pos
    mram_write_raw(pos, len, buf);
    mram_write_raw(pos + MRAM_REGION_SIZE, len, buf);
    mram_write_raw(pos + MRAM_REGION_SIZE * 2, len, buf);
}

void mram_read(long pos, long len, char *buf) {
    massert(pos < MRAM_REGION_SIZE);
    massert(pos + len < MRAM_REGION_SIZE);

    // Read len bytes from each region starting at index pos
    char r1_buf[MRAM_REGION_BUF_SIZE];
    mram_read_raw(pos, len, r1_buf);
    char r2_buf[MRAM_REGION_BUF_SIZE];
    mram_read_raw(pos + MRAM_REGION_SIZE, len, r2_buf);
    char r3_buf[MRAM_REGION_BUF_SIZE];
    mram_read_raw(pos + MRAM_REGION_SIZE * 2, len, r3_buf);

    // Validate the data, which we expect to have been the same in each region
    for (long i = 0; i < len; i++) {
        char valid;

        if (r1_buf[i] == r2_buf[i] && r1_buf[i] == r3_buf[i]) {
            // This byte was the same in each region
            valid = r1_buf[i];
        } else {
            // This byte was different in at least one region, so we compare it
            // across regions bit-by-bit
            int corrections = 8;
            valid = 0;
            for (int j = 0; j < 8; j++) {
                // Get the jth bit of this byte in each region
                char r1_bit = r1_buf[i] & (1 << j);
                char r2_bit = r2_buf[i] & (1 << j);
                char r3_bit = r3_buf[i] & (1 << j);

                // Set this bit in valid to 1 if it is 1 in two regions
                if ((r1_bit & r2_bit) || (r1_bit & r3_bit) || (r2_bit & r3_bit)) {
                    valid |= (1 << j);
                }

                // If this bit was the same in every region, this was not an
                // actual correction
                if (r1_bit == r2_bit && r1_bit == r3_bit) {
                    corrections--;
                }
            }
            warning("mram: byte verification failure resolved by %d bit corrections", corrections);

            // Rewrite the corrected byte to all regions
            mram_write(pos + i, 1, &valid);
        }

        // Put the validated byte in the return buffer
        if (buf != NULL) {
            buf[i] = valid;
        }
    }
}