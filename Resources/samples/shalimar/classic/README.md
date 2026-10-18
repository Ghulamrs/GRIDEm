# Shalimar examples - the classic set

The single-file programs of RIDE's own `programs` folder (`*.shm`), as `.shl` - the extension both IDEs know Shalimar by - each a project of GRIDEd (`classic.grws` opens all of them). Only the header comment changed, to name the new file. `gcd.shm` is the same program as `../gcd` and is not repeated; `rotmat` and `table` here are different programs from `../rotmat` and `../table`.

Each file ends with `// expected:` lines: what it prints (for `table`, rows 1, 5 and 10 of the ten).

| Example | What it shows |
|---|---|
| `factorial` | a loop and a recursive function: 10! |
| `fibonacci` | a sequence by a loop |
| `gaussseidel` | an iterative solver over a real matrix |
| `invert` | Gauss-Jordan inversion of a matrix, in place |
| `prime` | a sieve and a count of primes |
| `quadratic` | the roots of a quadratic |
| `rotations` | rotation matrices composed |
| `rotmat` | rotation matrices for yaw, roll and pitch (an older form of rotmat) |
| `sqroot` | square roots by Newton's method |
| `strsplit` | char arrays: a string split at its space |
| `table` | a times table built with ?? (print without a newline) |
