#include <stdlib.h>
#include "skidpack.h"

/* On a 16-bit host malloc cannot serve more than 64 KB, so buffers that must
 * hold a decompressed resource come from halloc/farmalloc instead. Elsewhere
 * these are plain malloc/realloc/free. */
/* Turbo C has no halloc. farmalloc is the equivalent, and its result is meant
 * to be used through a huge pointer, which is what rs_bytep already is. Built
 * and run with Turbo C 2.01, large model: the whole corpus reproduces. */
#if defined(__TURBOC__)
#    include <alloc.h>
#    define RS_ALLOC(n) ((rs_bytep)farmalloc((unsigned long)(n)))
#    define RS_FREE(p) farfree((void far *)(p))
/* What decides this is pointer size rather than which operating system is
 * underneath. A 32-bit DOS target has a flat address space and no halloc at
 * all, so asking about DOS answers the wrong question.
 * Watcom's own <malloc.h> puts halloc inside `#if defined(_M_I86)`, so testing
 * for DOS sends wcc386 -bt=dos into a branch its library cannot satisfy.
 * Microsoft C 5.10 predefines M_I86; Watcom 16-bit predefines both spellings.
 */
#elif defined(M_I86) || defined(_M_I86)
#    include <malloc.h>
#    define RS_ALLOC(n) ((rs_bytep)halloc((long)(n), 1))
#    define RS_FREE(p) hfree((void RS_HUGE *)(p))
#else
#    define RS_ALLOC(n) ((rs_bytep)malloc((size_t)(n)))
#    define RS_FREE(p) free((void *)(p))
#endif

void rs_buf_init(rs_buf *b)
{
    b->data = 0;
    b->len = 0;
    b->cap = 0;
    b->err = 0;
}

void rs_buf_free(rs_buf *b)
{
    if (b->data) RS_FREE(b->data);
    rs_buf_init(b);
}

/* Reallocate to exactly `cap`, which must be at least the current length. */
static int rs_buf_setcap(rs_buf *b, rs_size cap)
{
    rs_size  i;
    rs_bytep p;

    /* No realloc: the 16-bit far/huge allocators have no portable equivalent,
     * and copying keeps one code path on every host. */
    p = RS_ALLOC(cap);
    if (!p) {
        b->err = 1;
        return -1;
    }
    for (i = 0; i < b->len; ++i) p[i] = b->data[i];
    if (b->data) RS_FREE(b->data);
    b->data = p;
    b->cap = cap;
    return 0;
}

/* Ask for room up front when the eventual size is known or can be estimated.
 *
 * This matters far more on DOS than it looks. Growth doubles from 1 KB, and
 * with no realloc each step allocates the new block while the old one is still
 * held, so a buffer on its way to 64 KB transiently needs 96 KB and leaves the
 * far heap fragmented behind it. Three such buffers live at once during a
 * container compression. Reserving once removes both the churn and the peak,
 * On a real DOS machine that is what lets a 48 KB resource through at all.
 */
void rs_buf_reserve(rs_buf *b, rs_size want)
{
    if (b->err || want <= b->cap) return;
    rs_buf_setcap(b, want);
}

/* Grow to hold at least `need` more bytes. Sets err on failure and leaves the
 * buffer usable but unchanged, so callers can keep going and test once. */
static int rs_buf_grow(rs_buf *b, rs_size need)
{
    rs_size cap;

    if (b->err) return -1;

    /* b->len + need can wrap and then compare as small, which is how a
     * growth check turns into a heap overflow. Subtracting cannot. */
    if (need > RS_SIZE_MAX - b->len) {
        b->err = 1;
        return -1;
    }
    if (b->len + need <= b->cap) return 0;

    cap = b->cap ? b->cap : 1024;
    while (cap < b->len + need) {
        if (cap > RS_SIZE_MAX / 2) {
            cap = b->len + need;
            break;
        }
        cap *= 2;
    }
    return rs_buf_setcap(b, cap);
}

void rs_buf_push(rs_buf *b, unsigned char v)
{
    if (rs_buf_grow(b, (rs_size)1)) return;
    b->data[b->len++] = v;
}

void rs_buf_append(rs_buf *b, rs_cbytep p, rs_size n)
{
    rs_size i;
    if (n == 0) return;
    if (rs_buf_grow(b, n)) return;
    for (i = 0; i < n; ++i) b->data[b->len + i] = p[i];
    b->len += n;
}

/* Append a region of a buffer to itself.
 *
 * rs_buf_append cannot do this. It takes a pointer, and growing the buffer
 * allocates a new block and frees the old one, so a pointer into the buffer is
 * dangling by the time the copy runs. Holding an offset instead survives the
 * move. Copying one byte at a time is also required rather than merely tidy:
 * the source region and the destination can overlap once len exceeds off.
 */
int rs_buf_append_self(rs_buf *b, rs_size off, rs_size n)
{
    rs_size i;

    if (b->err) return -1;
    if (off > b->len || n > b->len - off) return -1; /* no wrap on off + n */
    if (n == 0) return 0;

    if (rs_buf_grow(b, n)) return -1;
    for (i = 0; i < n; ++i) b->data[b->len + i] = b->data[off + i];
    b->len += n;
    return 0;
}

rs_size rs_size24(rs_cbytep p)
{
    return (rs_size)p[0] | ((rs_size)p[1] << 8) | ((rs_size)p[2] << 16);
}

/* Refuses rather than truncates. A silently wrapped length yields a file that
 * looks well formed and decodes to the wrong size, which is worse than failing
 * to write one at all. The error is sticky, so a caller that ignores the
 * return value still cannot produce output. */
int rs_put24(rs_buf *b, rs_size v)
{
    if (v > RS_SIZE24_MAX) {
        b->err = 1;
        return -1;
    }
    rs_buf_push(b, (unsigned char)(v & 0xFF));
    rs_buf_push(b, (unsigned char)((v >> 8) & 0xFF));
    rs_buf_push(b, (unsigned char)((v >> 16) & 0xFF));
    return b->err ? -1 : 0;
}
