// check.h - an answer put back into A x = b
#ifndef CHECK_H
#define CHECK_H

#include "CMatrix.h"

// the largest magnitude among the residuals A x - b: row i of A against x, less b[i]
double worstResidual(const Mat3 &A, const Vec3 &x, const Vec3 &b);

#endif
