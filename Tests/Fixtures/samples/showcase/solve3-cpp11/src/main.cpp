// main.cpp - three equations in x, y and z
//
//    x +  y +  z = 25
//   2x -  y +  z = 16
//    x + 2y -  z =  6
//
// the same system as the Shalimar example solve3-shalimar, here with the classes
// of cpp-vector-exercise (github.com/Ghulamrs/cpp-vector-exercise, MIT licence):
// a CMatrix holds its rows as CVectors, A.det() and A.solve(b, x) eliminate whole
// rows at a time with partial pivoting, and check.cpp puts the answer back into
// the equations. The answer is x = 5, y = 7, z = 13.
#include <iomanip>
#include <iostream>
#include "check.h"      // and through it CMatrix.h and CVector.h - included once here: cpp11 1.8
                        // does not skip a second inclusion of CMatrix.h cleanly (fixed in cpp11 1.10)

int main()
{
    const Mat3 A = { {1.0,  1.0,  1.0},           // each row a CVector<3>, from its own
                     {2.0, -1.0,  1.0},           // initializer_list constructor
                     {1.0,  2.0, -1.0} };
    const Vec3 b = {25.0, 16.0, 6.0};
    Vec3 x;

    std::cout << std::fixed << std::setprecision(7);
    std::cout << "det A = " << A.det() << "\n";
    if (!A.solve(b, x)) {
        std::cout << "the system is singular\n";
        return 1;
    }
    const char *names[] = {"x", "y", "z"};
    for (int i = 0; i < x.size(); ++i) std::cout << names[i] << " = " << x[i] << "\n";
    std::cout << (worstResidual(A, x, b) <= 1e-9 ? "check: all three equations hold"
                                                 : "check: the answer does not fit") << "\n";
    return 0;
}
// expected: det A = 7.0000000
// expected: x = 5.0000000
// expected: y = 7.0000000
// expected: z = 13.0000000
// expected: check: all three equations hold
