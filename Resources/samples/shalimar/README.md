# Shalimar examples

RIDE 5.12's Shalimar examples (`programs\*.shl` and `projects\shl-*`), unchanged, each as a project of GRIDEd.

Open `shalimar.grws`: the seven are one workspace. Choose the startup project (right-click > Set as Startup Project), the target (Windows or TMS320C6747) and the configuration, then Start Without Debugging (Ctrl+F5) to run it or Start Debugging (F5) to step through it.

Every Shalimar file has a `main()`: a project's program is its first file, as RIDE's `build.target` named it; the other files are compiled along with it (shalimar notes them, "also compiled ...").

| Example | Files | What it shows |
|---|---|---|
| `hello` | `hello.shl` | the first program: one line printed |
| `gcd` | `gcd.shl` | a while loop: Euclid's algorithm on 48 and 18 |
| `primes` | `primes.shl` | nested loops and an early exit: the primes below 60 |
| `table` | `table.shl` | a for loop and several values to a line: a 5 x 5 table |
| `rotmat` | `rotmat.shl` | reals, 2-D arrays and functions: rotation matrices for yaw, roll and pitch |
| `shl-matrix` | `matrix.shl`, `linalg.shl` | a program in two files: matrix.shl's main() calls mul and trace from linalg.shl |
| `shl-primes` | `primes.shl`, `numbers.shl` | a program in two files: twin primes, a count and a gcd from numbers.shl |

## What each prints

`hello`:

```
Hello from Shalimar
```

`gcd`:

```
gcd of 48 18
is 6
```

`primes`:

```
primes below 60
2
3
5
...
59
done
```

`table`:

```
1 2 3 4 5
2 4 6 8 10
3 6 9 12 15
4 8 12 16 20
5 10 15 20 25
```

`rotmat`:

```
yaw 90
 0.000000   1.000000   0.000000
-1.000000   0.000000   0.000000
 0.000000   0.000000   1.000000
roll 90
 1.000000   0.000000   0.000000
 0.000000   0.000000   1.000000
 0.000000  -1.000000   0.000000
roll pitch yaw 90
 0.000000   0.000000   1.000000
-0.000000  -1.000000   0.000000
 1.000000  -0.000000   0.000000
```

`shl-matrix`:

```
trace a: 3.0000000
trace a*a: 3.0000000
row 0: 1.0000000 4.0000000 6.0000000
```

`shl-primes`:

```
twins 3 5
twins 5 7
...
twins 41 43
primes below 50: 15
gcd 1071 462: 21
```
