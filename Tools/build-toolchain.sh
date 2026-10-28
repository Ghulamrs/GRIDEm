#!/bin/bash
# =============================================================================
#  build-toolchain.sh - builds the eleven tools GRIDEm drives, for this machine,
#  into GRIDEm's own build folder (reference section 6). Nothing is written
#  inside GRIDE1 or the tool repositories:
#    - the sources are GRIDE1's Toolchain/ (the copy GRIDEd and GRIDE 2.0 build) - except cpp11,
#      taken whole from C++Optimize's last commit -
#      copied into $OUT/src with the same Compilers/ TI/ Windows/ layout;
#    - GRIDE1's copy leaves out the tools' Makefiles, so each comes from the tool's
#      own repository under Developer/Claude (C++Optimize/, Compilers/, TI/, Windows/);
#    - the programs land in $OUT/bin, the runtimes in $OUT/lib.
#  `make app` then copies bin/* into GRIDEm.app/Contents/MacOS and lib/ into
#  Contents/Resources/lib.
#
#    Tools/build-toolchain.sh            all tools
#    Tools/build-toolchain.sh cpp11 c90  just these (cpp11 c90 shalimar c2s asm6x lnk6x
#                                        sim6747 vm6747 masm link rts6x)
#  Environment: GRIDE1 (default ../GRIDE1), TOOL_REPOS (default ..), OUT (default build/toolchain),
#  JOBS (default the processor count).
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
GRIDE1="${GRIDE1:-$HERE/../GRIDE1}"
TOOL_REPOS="${TOOL_REPOS:-$HERE/..}"
OUT="${OUT:-$HERE/build/toolchain}"
SRC="$OUT/src"
BIN="$OUT/bin"
LIB="$OUT/lib"
JOBS="${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) )}"

say() { printf '\n== %s\n' "$*"; }
die() { printf 'build-toolchain: %s\n' "$*" >&2; exit 1; }

# GRIDE1's copy of the toolchain: Toolchain/ (toolchain/ before GRIDEx3)
TC_SRC="$GRIDE1/Toolchain"
[ -d "$TC_SRC" ] || TC_SRC="$GRIDE1/toolchain"
[ -d "$TC_SRC" ] || die "no toolchain at $GRIDE1/Toolchain (set GRIDE1)"
mkdir -p "$SRC" "$BIN" "$LIB"

# tool word -> tree under toolchain/ (and under TOOL_REPOS)
tree_of() {
  case "$1" in
    c90) echo Compilers/Compiler-Ci ;;      cpp11) echo Compilers/C++Optimize ;;
    shalimar) echo Compilers/Compiler-Si ;; c2s) echo Compilers/Converter-C2S ;;
    asm6x) echo TI/ASM6x ;;  lnk6x) echo TI/LNK6x ;;  rts6x) echo TI/RTS6x ;;
    sim6747) echo TI/SIM6747 ;;  vm6747) echo TI/VM6747 ;;
    masm) echo Windows/MASM ;;  link) echo Windows/LINK ;;
    *) die "unknown tool '$1'" ;;
  esac
}

# where a tool's own repository is: C++Optimize sits in Developer/Claude itself (moved there from
# Compilers/), the others under Compilers/, TI/ and Windows/
repo_of() {
  local t; t="$(tree_of "$1")"
  if [ "$1" = cpp11 ] && [ -f "$TOOL_REPOS/C++Optimize/Makefile" ]; then echo "$TOOL_REPOS/C++Optimize"
  else echo "$TOOL_REPOS/$t"; fi
}

# the sources of one tree, refreshed from GRIDE1, with the repository's Makefile beside them
stage() {
  local t; t="$(tree_of "$1")"
  local from="$TC_SRC/$t" to="$SRC/$t" repo; repo="$(repo_of "$1")"
  # cpp11 comes whole from its own repository's last commit (1.21 on), not from GRIDE1's older
  # copy (1.11, which refuses an unnamed template parameter). Its Makefile compiles every
  # src/*.cpp it finds and keeps objects in obj/, so the staged tree is replaced, not overlaid.
  if [ "$1" = cpp11 ] && [ -e "$repo/.git" ]; then
    rm -rf "$to"; mkdir -p "$to"
    git -C "$repo" archive HEAD Makefile src include lib ide msvc tools | (cd "$to" && tar xf -)
    return
  fi
  [ -d "$from" ] || die "$1: no sources at $from"
  [ -f "$repo/Makefile" ] || die "$1: no Makefile at $repo/Makefile (set TOOL_REPOS)"
  mkdir -p "$to"
  # copy (not rsync --delete: nothing is removed from a build folder by this script)
  (cd "$from" && tar cf - --exclude='*.vcxproj*' --exclude='x64' --exclude='build' .) | (cd "$to" && tar xf -)
  cp "$repo/Makefile" "$to/Makefile"
  # what a Makefile's `all` needs and GRIDE1's minimal copy left out
  # (a folder GRIDE1 kept part of - RTS6x's tools/ holds only ar6x there - gets the missing files)
  for extra in tools workspace.mk; do
    [ -e "$repo/$extra" ] || continue
    if [ ! -e "$to/$extra" ]; then cp -R "$repo/$extra" "$to/$extra"
    elif [ -d "$repo/$extra" ]; then cp -R -n "$repo/$extra/." "$to/$extra/" 2>/dev/null || true
    fi
  done
}

build_simple() {   # word  -> $BIN/<exe>
  local w="$1" t; t="$(tree_of "$w")"
  say "$w"
  stage "$w"
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$BIN" all
}

build_shalimar() {
  local t; t="$(tree_of shalimar)"
  say "shalimar (compiler, host runtimes, C6747 runtime assembly)"
  stage shalimar
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$BIN" BUILD="$OUT/obj/Compiler-S" all
  [ -x "$BIN/cpp11.exe" ] || die "shalimar's C6747 runtime needs cpp11 first"
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$BIN" BUILD="$OUT/obj/Compiler-S" CXX1="$BIN/cpp11.exe" tms6747
}

build_rts6x() {
  local t; t="$(tree_of rts6x)"
  say "rts6x (rts6x, printf6x and the C6747 Shalimar runtime libraries)"
  stage rts6x
  [ -x "$BIN/cpp11.exe" ] && [ -x "$BIN/asm6x.exe" ] || die "rts6x needs cpp11 and asm6x first"
  local rdir="$LIB/rts6x-tms6747"
  mkdir -p "$rdir"
  make -C "$SRC/$t" -j"$JOBS" BINDIR="$rdir" OBJDIR="$OUT/obj/RTS6x/obj" \
       CPP11="$BIN/cpp11.exe" ASM6X="$BIN/asm6x.exe" all
  # shmrt6x.lib / shmrt6xd.lib: Toolchain.targets' PackObjects - the assembly shalimar's build wrote,
  # through asm6x, packed by ar6x
  local ar6x="$rdir/ar6x.exe"
  [ -x "$ar6x" ] || die "rts6x's build left no ar6x at $ar6x"
  local pair
  for pair in "shmrt-tms6747:shmrt6x.lib" "shmrt-tms6747-debug:shmrt6xd.lib"; do
    local asmdir="$BIN/lib/${pair%%:*}" libname="${pair##*:}"
    local objdir="$OUT/obj/RTS6x/${libname%.lib}"
    [ -d "$asmdir" ] || die "no Shalimar C6747 runtime assembly at $asmdir (build shalimar first)"
    mkdir -p "$objdir"
    "$BIN/asm6x.exe" "$asmdir/*.s" -o "$objdir"
    ls "$objdir"/*.obj | sort > "$objdir.list"
    "$ar6x" -r "$rdir/$libname" @"$objdir.list"
    echo "rts6x: $rdir/$libname ($(wc -l < "$objdir.list" | tr -d ' ') members)"
  done
}

# shalimar writes its runtimes into $BIN/lib; GRIDEm looks in Resources/lib
collect_lib() {
  if [ -d "$BIN/lib" ]; then
    (cd "$BIN/lib" && tar cf - .) | (cd "$LIB" && tar xf -)
  fi
}

ALL="cpp11 c90 shalimar c2s asm6x lnk6x sim6747 vm6747 masm link rts6x"
WANT="${*:-$ALL}"
for w in $ALL; do
  case " $WANT " in *" $w "*) ;; *) continue ;; esac
  case "$w" in
    shalimar) build_shalimar ;;
    rts6x)    build_rts6x ;;
    *)        build_simple "$w" ;;
  esac
done
collect_lib

say "done"
ls -l "$BIN" | sed -n '1,40p'
echo "runtimes in $LIB:"; ls "$LIB"
