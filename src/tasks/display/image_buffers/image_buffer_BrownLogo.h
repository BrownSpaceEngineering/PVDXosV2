#ifndef IMAGE_BUFFER_BROWNLOGO_H
#define IMAGE_BUFFER_BROWNLOGO_H

#include <stdint.h>

// Uncomment (or pass -DDISPLAY_BROWNLOGO) to build in the Brown logo. It is off by default because the application
// runs from RAM, so even this const image costs 8 KB of RAM.
// #define DISPLAY_BROWNLOGO

#ifdef DISPLAY_BROWNLOGO
extern const uint8_t IMAGE_BUFFER_BROWNLOGO[8192];
#endif

#endif // IMAGE_BUFFER_BROWNLOGO_H
