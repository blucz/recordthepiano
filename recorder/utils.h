#ifndef INCLUDED_UTILS_H
#define INCLUDED_UTILS_H

typedef struct lineparser lineparser_t;

#ifndef LINEPARSER_MAX_LINE
#    define LINEPARSER_MAX_LINE 255
#endif

typedef int (*lineparser_cb_t)(void *userdata, char *line, int len);

void lineparser_init(lineparser_t*self, lineparser_cb_t cb, void *userdata);
void lineparser_destroy(lineparser_t*self);

/* returns the number of bytes consumed from the buffer.
 *
 * this always equal to len unless callback returned TRUE to end processing early
 */
int lineparser_write(lineparser_t*self, const void *buf, int off, int len);

struct lineparser
{
    char *bufp;
    lineparser_cb_t cb;
    void *userdata;
    char buf[LINEPARSER_MAX_LINE + 1];
};

void perrorf(const char *s, const char *fmt, ...);
void failf(const char *fmt, ...);
void tracef(const char *fmt, ...);
long long now_us();

#endif
