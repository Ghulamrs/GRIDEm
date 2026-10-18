/* stats.h - the sum, average, smallest and largest of a series */
#ifndef STATS_H
#define STATS_H

struct summary {
    long sum;
    double average;
    int smallest, largest;
};

/* n values from v; n must be at least 1 */
struct summary summarise(const int *v, int n);

#endif
