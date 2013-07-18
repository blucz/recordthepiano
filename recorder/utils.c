#include "utils.h"

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

void lineparser_init(lineparser_t *self, lineparser_cb_t cb, void *userdata) {
    self->bufp = self->buf;
    self->userdata = userdata;
    self->cb = cb;
}

void lineparser_destroy(lineparser_t *self) { (void)self; }

int lineparser_write(lineparser_t *self, const void *buf, int off, int len)
{
    int ret = 0;
    const char *cbuf = (const char*)buf;
    char *maxbufp = self->buf + LINEPARSER_MAX_LINE;
    for ( ; len ; off++, len--) {
        ret++;
        if (cbuf[off] == '\n' || cbuf[off] == '\r' || self->bufp == maxbufp) {
            if (len > 1 && cbuf[off] == '\r' && cbuf[off + 1] == '\n')
                off++, len--, ret++;
            *self->bufp = '\0';
            if (self->bufp != self->buf) {
                int len = self->bufp - self->buf;
                self->bufp = self->buf;
                if (self->cb(self->userdata, self->buf, len)) break;
            }
        } else {
            *self->bufp++ = cbuf[off];
        }
    }
    return ret;
}

void perrorf(const char *s, const char *fmt, ...) {
    perror(s);
    va_list ap;
    va_start(ap, fmt); 
    fprintf(stderr, "[recordthepiano] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void failf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt); 
    fprintf(stderr, "[recordthepiano] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void tracef(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt); 
    fprintf(stderr, "[recordthepiano] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

long long now_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + (long long)tv.tv_usec;
}

