/* rng.c - the C standard's example generator (ISO C 7.20.2.2):
 * next = next * 1103515245 + 12345, and bits 16..30 of it. The
 * state is kept to 32 bits, so an unsigned long of 64 bits
 * (Linux, macOS) gives the same series as one of 32 (Windows,
 * the C6747).
 */
#include "rng.h"

static unsigned long state = 1;

void rng_seed(unsigned long seed)
{
    state = seed & 0xFFFFFFFFUL;
}

int rng_next(void)
{
    state = (state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (int)((state >> 16) & RNG_MAX);
}

/* A value from lo to hi. The remainder favours the low values a
 * little when the span does not divide RNG_MAX + 1; for a span
 * this small against 32768 the difference cannot be seen. */
int rng_range(int lo, int hi)
{
    return lo + rng_next() % (hi - lo + 1);
}
