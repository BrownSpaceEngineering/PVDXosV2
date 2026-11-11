#ifndef BOOT_LOG_H
#define BOOT_LOG_H

/*
 * Stage-2 log over SEGGER RTT up-channel 1, the channel tools/rtt.sh captures. This is a minimal writer, not the
 * SEGGER library (which does not fit the 8 KiB slot). The control block is pinned to the start of RAM by both the
 * bootloader and OS linker scripts, so one host session follows the boot into the OS.
 * Non-blocking: a line is dropped when it does not fit (e.g. no host attached).
 */

void boot_log_init(void);

// Minimal printf: %s, %u, %x, %0<width>x and %%. A line longer than 96 characters is truncated.
void boot_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Wipes the control block before the OS starts, so the host never follows stale stage-2 buffer pointers.
void boot_log_shutdown(void);

#endif
