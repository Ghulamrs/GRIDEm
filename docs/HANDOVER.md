# GRIDEm - hand-over to the first build on the Mac

Written 2026-10-18 at the end of the generation session. The reference is
[`GRIDEm-Design.md`](GRIDEm-Design.md); the working rules are in [`PORTING.md`](PORTING.md).
Nothing here is in git yet (where GRIDEm goes in git is still to be decided).

## What exists

| Part | Lines | State |
|---|---|---|
| `Sources/Core`, `Platform`, `Settings`, `Model`, `Services` (C++14) | ~18,700 | every file compiles under `-std=c++14 -pedantic -Wall -Wextra -Werror` (g++ 11, Linux); `build/libGRIDEm.a` builds |
| `Sources/UI` (Objective-C++, AppKit) | ~19,500 | written and statically cross-checked; **never compiled** (no clang/AppKit where it was written) |
| `Tests/*.mm` (XCTest) + `Tests/Fixtures` | ~2,000 + data | written; **never run** |
| `Tools/` | | `GRIDEm-build`, `GRIDEm-toolcheck` build and run; `build-toolchain.sh` ran on Linux; `make-xcodeproj.py` writes a project Xcode has not opened yet |
| `Resources/` | | `Info.plist`, `toolchain.json` (the Mac as host; `toolchain-diff.py` agrees with GRIDEd's), `lib/ccs`, `docs`, `samples`, `Credits.rtf`; no icon yet |

## What was proved (on the Linux shell of this Mac's VM)

- The library builds under the contract's flags; `make check` passes.
- `GRIDEm-toolcheck --toolchain Resources/toolchain.json`: 4 targets, 22 tools, 5 libraries, 17 compiler
  options, **0 problems**, host target `arm64-darwin`.
- `Tools/build-toolchain.sh` built all eleven tools (cpp11, c90, shalimar + runtimes, c2s, asm6x, lnk6x,
  sim6747, vm6747, masm, link, rts6x + shmrt6x[d].lib) from `../GRIDE1/Toolchain` with the tool
  repositories' Makefiles, writing nothing inside GRIDE1 or the repositories.
- `GRIDEm-build showcase.grws --config Debug --target TMS320C6747`: the three showcase projects (C90,
  C++11, Shalimar) compile, assemble and link; each `.out` runs on `sim6747 --run` with the right output.
- The debugger services, driven as `MainWindow+Debug.mm` drives them, on sim6747 and vm6747: breakpoint
  stops, locals (name/value/type/address), call stack, watches, step over/into/out, registers, memory,
  exit status and program output; the Shalimar session too. The transcripts are in
  `Tests/Fixtures/sessions/`.
- Settings, `.pro` and `.grws` files round-trip byte for byte; GRIDEd's own files read back identical.

## First steps on the Mac (for the local Claude)

1. `Tools/build-toolchain.sh` - the tools for arm64-darwin into `build/toolchain` (needs read access to
   `../GRIDE1` and `../C++Optimize`, `../Compilers`, `../TI`, `../Windows`).
2. `make lib && make tools && ./build/GRIDEm-toolcheck` - the library with Apple clang. Apple's libc++
   lets C++17 names through under `-std=c++14`; the Linux g++ build is the strict one.
3. `make app` - the first compile of `Sources/UI`. Expect a round of errors: Objective-C argument types
   were not checked by a compiler (a `std::string` where an `NSString*` is wanted and the like).
   Fix them in place, one commit per fix with its regression test where one makes sense.
4. `make xcodeproj && make test` - the XCTest suite. `ParityTests` skips until golden files from GRIDEd
   exist (`Tests/Fixtures/golden/README.md` says how to capture them on the Windows box).
5. Open `build/GRIDEm.app`, open `Contents/Resources/samples/showcase/showcase.grws`, build for the
   TMS320C6747, run, debug.

## To know

- **TextKit 1, not TextKit 2.** The reference (section 4) says TextKit 2; the editor uses TextKit 1
  because temporary-attribute colouring and the `NSRulerView` gutter need it. This needs the user's
  agreement and an amendment to the reference.
- **The compilers' own headers**: c90 and cpp11 compile their header folders in as absolute paths
  (`CXX1_INCLUDE_DIR`, `CC1_INCLUDE_DIR` = `build/toolchain/src/...`). The app works on this Mac; a copy
  on another Mac needs those folders shipped in `Resources` and given as the installation's include
  directories.
- **Shalimar's host runtime** is found by shalimar in `lib/` beside it, then `../lib`; `make app` links
  `Contents/lib -> Resources/lib` for it. Check that ad-hoc signing accepts it.
- **Deleting in the GRIDEm folder was not permitted during generation**, so `build/` holds stray `st*`
  files from `ar` and `make clean` could not be tried. Remove them when you can.
- Behaviours carried over from GRIDEd unchanged (found while testing; they are the same on Windows):
  vm6747's Step Out from `main` ends the session; the debugger's own lines and output without a final
  newline can show up as program output; the call stack stops at `main`; cpp11's compiler-made locals
  (`.ilbacking0`) appear in Locals.
- The program runner hides the terminal's echo of typed input by dropping its first echo; a cleaner way
  is an echo switch in `Platform::Process` for terminal mode.
- Windows-only parts (Visual Studio, `cl`, `link.exe`, `ml64`, CodeView `clang`, `cdb`, setup scripts)
  are kept in the files and say "Windows only" on the Mac; a same-named Mac program (`/usr/bin/link`,
  Xcode's `clang`) is never taken for them.

## Still open (reference section 9)

Which git repository GRIDEm goes into; one shared core for GRIDEd later; "Build for Windows" on the
Mac (masm and link do build here); signing (ad hoc for now).
