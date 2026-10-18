/* rng.h - a small random number generator of the program's own
 *
 * The C library's rand() is each platform's own: Microsoft's on
 * Windows, RTS6x's on the TMS320C6747, glibc's on Linux - three
 * different series from the same seed. This one is the generator
 * the C standard gives as its example (ISO C 7.20.2.2), written
 * here so the series is the same wherever the program runs, and
 * an expected output can be checked.
 */
#ifndef RNG_H
#define RNG_H

void rng_seed(unsigned long seed);
int rng_next(void);                /* 0 .. RNG_MAX */
int rng_range(int lo, int hi);     /* lo .. hi, both included */

#define RNG_MAX 32767

#endif
