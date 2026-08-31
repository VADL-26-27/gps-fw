#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void _exit(int status) {
  (void)status;

  for (;;) {
  }
}

/*
 * Treat stdout and stderr as a null device.
 * printf() succeeds, but produces no visible output.
 */
int _write(int fd, char *buffer, int length) {
  (void)buffer;

  if ((fd == STDOUT_FILENO) || (fd == STDERR_FILENO)) {
    return length;
  }

  errno = EBADF;
  return -1;
}

/* Treat standard input as permanently at end-of-file. */
int _read(int fd, char *buffer, int length) {
  (void)buffer;
  (void)length;

  if (fd == STDIN_FILENO) {
    return 0;
  }

  errno = EBADF;
  return -1;
}

int _close(int fd) {
  (void)fd;

  errno = ENOSYS;
  return -1;
}

/* UART-style streams cannot seek. */
off_t _lseek(int fd, off_t offset, int whence) {
  (void)fd;
  (void)offset;
  (void)whence;

  errno = ESPIPE;
  return (off_t)-1;
}

int _fstat(int fd, struct stat *status) {
  (void)fd;

  status->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd) {
  return (fd == STDIN_FILENO) || (fd == STDOUT_FILENO) || (fd == STDERR_FILENO);
}

/*
 * Disable newlib malloc(). FreeRTOS heap_4 remains available through
 * pvPortMalloc(), because it does not use _sbrk().
 */
void *_sbrk(ptrdiff_t increment) {
  (void)increment;

  errno = ENOMEM;
  return (void *)-1;
}