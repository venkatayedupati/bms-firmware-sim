/*
 * Retargets newlib's _write() to the UART, so printf()/vprintf() in
 * src/util/logger.c work completely unchanged on this target -- exactly
 * the point of the whole OSAL port: nothing outside src/osal/ (and this
 * target-specific board support code) needs to know it's not running on
 * pthreads anymore.
 *
 * Linked with -specs=nosys.specs, which already stubs every other syscall
 * newlib might reference (_read, _close, _lseek, _fstat, _isatty, ...);
 * _write is the only one this project's logging path actually needs.
 */
#include "uart.h"
#include <sys/stat.h>
#include <unistd.h>

ssize_t _write(int fd, const void *buf, size_t count) {
    (void)fd; /* stdout and stderr both go to the same UART; nothing here distinguishes them */
    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        if (p[i] == '\n') uart_putc('\r'); /* most serial terminals expect CRLF */
        uart_putc(p[i]);
    }
    return (ssize_t)count;
}
