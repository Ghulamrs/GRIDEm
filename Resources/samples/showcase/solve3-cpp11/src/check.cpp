// check.cpp - the residual, with CMatrix * CVector and CVector arithmetic
#include "check.h"

#include <cmath>

double worstResidual(const Mat3 &A, const Vec3 &x, const Vec3 &b)
{
    const Vec3 r = A * x - b;            // a matrix times a vector, less a vector
    double worst = 0.0;
    for (int i = 0; i < r.size(); ++i)
        if (std::fabs(r[i]) > worst) worst = std::fabs(r[i]);
    return worst;
}
