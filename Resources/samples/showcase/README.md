# Showcase - one project of several files for each compiler

Three small programs to study, each split into files the way a larger program would be, each
checking its own result, and each giving the same output on Windows and on the TMS320C6747.
The `// expected:` lines at the end of each program's main file are what it prints.

| Project | Compiler | What it shows |
|---|---|---|
| `solve3-shalimar` | shalimar | Three equations in x, y and z solved by Gaussian elimination with partial pivoting (`gauss.shl`), the answer put back into the equations (`check.shl`). Functions of the other files are called by name; each library file has a small `main` of its own that runs when the file is built alone. |
| `average-c90` | c90 | The average of a series of N random values, where N is itself drawn (10 to 100): a random number generator of the program's own (`rng.c`), the series `malloc`'d for exactly N values, one pass for sum, average, smallest and largest (`stats.c`). Give a seed as the program's argument for another N and another series. |
| `solve3-cpp11` | cpp11 | The same three equations as the Shalimar project, on the classes of [cpp-vector-exercise](https://github.com/Ghulamrs/cpp-vector-exercise) (`CVector.h`, `CMatrix.h`, unchanged, MIT licence): a `CMatrix` holds its rows as `CVector`s, `A.det()` and `A.solve(b, x)` eliminate whole rows with partial pivoting, and `check.cpp` puts the answer back (`A * x - b`). Templates with a size parameter, operator overloading, `std::ostream` formatting. |

The system both solvers take, whose answer is x = 5, y = 7, z = 13:

     x +  y +  z = 25
    2x -  y +  z = 16
     x + 2y -  z =  6

Why the C project brings its own generator: `rand()` is each platform's library's, so the same
seed gives one series on Windows (Microsoft's runtime) and another on the C6747 (RTS6x's). The
generator in `rng.c` is the C standard's own example, written into the program, so the series -
and so N and the average - are the same everywhere.

Open `showcase.grws` (GRIDEd) or a project's `.pro` (GRIDE 2.0), choose the target, and run.
