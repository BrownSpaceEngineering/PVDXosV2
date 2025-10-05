#ifndef CSP_ENDIAN_FIX_H
#define CSP_ENDIAN_FIX_H

#include <stdint.h>
#include <machine/endian.h>

// Define macros if missing
#ifndef htobe16
static inline uint16_t htobe16(uint16_t x) {
#if BYTE_ORDER == LITTLE_ENDIAN
	return (x >> 8) | (x << 8);
#else
	return x;
#endif
}
#endif

#ifndef be16toh
#define be16toh(x) htobe16(x)
#endif

#ifndef htobe32
static inline uint32_t htobe32(uint32_t x) {
#if BYTE_ORDER == LITTLE_ENDIAN
	return ((x >> 24) & 0x000000FF) |
		   ((x >> 8) & 0x0000FF00) |
		   ((x << 8) & 0x00FF0000) |
		   ((x << 24) & 0xFF000000);
#else
	return x;
#endif
}
#endif

#ifndef be32toh
#define be32toh(x) htobe32(x)
#endif

#ifndef htobe64
static inline uint64_t htobe64(uint64_t x) {
#if BYTE_ORDER == LITTLE_ENDIAN
	return ((uint64_t)htobe32(x & 0xFFFFFFFF) << 32) |
		   htobe32(x >> 32);
#else
	return x;
#endif
}
#endif

#ifndef be64toh
#define be64toh(x) htobe64(x)
#endif

#endif  // CSP_ENDIAN_FIX_H
