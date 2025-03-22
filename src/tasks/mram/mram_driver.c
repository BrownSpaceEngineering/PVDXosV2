// TODO: verify that MRAM starts in 1S-1S-1S transfer mode

#include "../../../ASF/driver_init.h"
#include "../../../ASF/hal/include/hal_spi_m_sync.h"
#include "../../../ASF/hal/include/hpl_spi.h"

#define massert(x) ((x) ? 0 : fatal("massert failed"));

#define MRAM_MAX_ADDRESS    (1 << 24)

#define MRAM_CS_LOW()       gpio_set_pin_level(0, 0)
#define MRAM_CS_HIGH()      gpio_set_pin_level(0, 1)

#define MRAM_CMD_READ       0x03
#define MRAM_CMD_WRITE_EN   0x06
#define MRAM_CMD_WRITE      0x02

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