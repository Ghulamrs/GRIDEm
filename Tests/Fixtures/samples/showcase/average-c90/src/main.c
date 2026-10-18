/* main.c - the average of a series of N random values
 *
 * N itself is drawn from the generator (10 to 100), so it changes
 * with the seed: the series is malloc'd for exactly N values,
 * filled with values from 0 to 999, and summarised (stats.c).
 *
 *   average           seed 2026, the run checked below
 *   average 7         another seed: another N, another series
 */
#include <stdio.h>
#include <stdlib.h>
#include "rng.h"
#include "stats.h"

int main(int argc, char *argv[])
{
    unsigned long seed = 2026;
    int n, i;
    int *values;
    struct summary s;

    if (argc > 1) seed = strtoul(argv[1], NULL, 10);
    rng_seed(seed);

    n = rng_range(10, 100);
    values = (int *)malloc(n * sizeof *values);
    if (values == NULL) {
        printf("no room for %d values\n", n);
        return 1;
    }
    for (i = 0; i < n; i++) values[i] = rng_range(0, 999);

    s = summarise(values, n);
    printf("seed %lu: N = %d\n", seed, n);
    printf("first five: %d %d %d %d %d\n", values[0], values[1], values[2], values[3], values[4]);
    printf("sum = %ld\n", s.sum);
    printf("average = %.3f\n", s.average);
    printf("smallest = %d, largest = %d\n", s.smallest, s.largest);

    free(values);
    return 0;
}
/* expected: seed 2026: N = 18
 * expected: first five: 958 587 582 544 655
 * expected: sum = 9207
 * expected: average = 511.500
 * expected: smallest = 46, largest = 958
 */
