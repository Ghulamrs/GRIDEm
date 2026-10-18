/* stats.c - one pass over the series */
#include "stats.h"

struct summary summarise(const int *v, int n)
{
    struct summary s;
    int i;

    s.sum = 0;
    s.smallest = v[0];
    s.largest = v[0];
    for (i = 0; i < n; i++) {
        s.sum += v[i];
        if (v[i] < s.smallest) s.smallest = v[i];
        if (v[i] > s.largest) s.largest = v[i];
    }
    s.average = (double)s.sum / n;
    return s;
}
