#ifndef MRAM_DRIVER_H
#define MRAM_DRIVER_H

#include "../../../ASF/driver_init.h"
#include "../../../ASF/hal/include/hal_spi_m_sync.h"
#include "../../../ASF/hal/include/hpl_spi.h"

#define MRAM_MAX_ADDRESS        8388608 // 8MiB
#define MRAM_REGION_SIZE        1000000
#define MRAM_REGION_BUF_SIZE    4096

#define MRAM_CS_LOW()           gpio_set_pin_level(0, 0)
#define MRAM_CS_HIGH()          gpio_set_pin_level(0, 1)

#define MRAM_CMD_READ           0x03
#define MRAM_CMD_WRITE_ENABLE   0x06
#define MRAM_CMD_WRITE          0x02
#define MRAM_CMD_READ_STATUS    0x05

#define massert(x) ((x) ? 0 : fatal("massert failed"));

void mram_init(void);

void mram_write_raw(long pos, long len, const char *buf);
void mram_read_raw(long pos, long len, char *buf);

void mram_write(long pos, long len, const char *buf);
void mram_read(long pos, long len, char *buf);

#endif