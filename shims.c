/* shims.c - libc functions the CVA6 bare-metal runtime does not provide.
 *
 * syscalls64.c supplies memcpy, memset, printf, puts, putchar, exit, abort.
 * The Falcon library additionally calls memmove (16 call sites); memcmp is
 * added because GCC can emit implicit calls to it even when the source
 * never names it.
 */
#include <stddef.h>
#include <stdint.h>

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {                       /* overlapping, copy backwards */
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (; n--; p++, q++) {
        if (*p != *q) return (int)*p - (int)*q;
    }
    return 0;
}

/* NOTE: strlen, strcmp and strcpy ARE provided by syscalls64.c.
   Only memmove and memcmp are missing there. */
