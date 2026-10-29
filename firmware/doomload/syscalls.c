/* Minimal newlib-nano hooks: bounded heap, no files, no console */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

extern char _ebss, _heap_end;
static char *heap_top = &_ebss;

void *_sbrk(int incr)
{
    if (heap_top + incr > &_heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *prev = heap_top;
    heap_top += incr;
    return prev;
}

uint32_t heap_free(void) { return (uint32_t)(&_heap_end - heap_top); }

int _write(int fd, const void *buf, int n) { (void)fd; (void)buf; return n; }
int _read(int fd, void *buf, int n)        { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd)                         { (void)fd; return -1; }
int _lseek(int fd, int off, int whence)    { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st)        { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                        { (void)fd; return 1; }
int _getpid(void)                          { return 1; }
int _kill(int pid, int sig)                { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int code)                       { (void)code; for (;;) {} }
