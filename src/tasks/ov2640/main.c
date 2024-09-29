#include <atmel_start.h>
#include "OV2640_Camera_Commands.h"

uint8_t readbuf[20480];

int main(void) {
	/* Initializes MCU, drivers and middleware */
	atmel_start_init();

    OV2640_init(); // FIXME: this is taking a really long time!
    OV2640_set_resolution(OV2640_160x120);
    uint32_t length = OV2640_capture(readbuf);

    struct io_descriptor *io;
    usart_sync_get_io_descriptor(&USART_0, &io);
    usart_sync_enable(&USART_0);

    uint8_t header[] = {0x75, 0x03, 0x30, 0x75, 0x03, 0x30};
    io_write(io, (uint8_t *)header, sizeof(header));

    // Send the image size -- MSBs first
    // (N.b. this can only handle images up to ~65 KB, which may not be large enough)
    uint8_t len_msbs = (length >> 8) & 0xff;
    uint8_t len_lsbs = length & 0xff;
    io_write(io, &len_msbs, 1);
    io_write(io, &len_lsbs, 1);

    int32_t bytes_written = io_write(io, (uint8_t *)readbuf, length);
    if (bytes_written != length) {
        ASSERT(0);
    }
}
