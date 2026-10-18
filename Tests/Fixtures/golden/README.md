# Golden build lines (reference section 7.2)

GRIDEd's Output pane names every program it runs and the file each one writes, one
progress line per step:

```
[c90]  main.c  ->  obj/Debug/C6747/main.s
[asm6x]  main.s  ->  obj/Debug/C6747/main.obj
```

GRIDEm's `BuildEngine` must write the same progress lines for the same workspace,
configuration and target. `ParityTests.mm` checks that for every `<name>.txt` here;
with none here it is skipped.

## Capturing a golden file (on the Windows box, with GRIDEd)

1. Open the workspace in GRIDEd (`C:\Users\GRA\work\GRIDEd\...`), choose the configuration
   and the target - TMS320C6747 is the one both machines can build in full.
2. **Build > Rebuild Workspace** (a rebuild, so no step is skipped as up to date).
3. In the Output pane (Build), select all and copy; save it as `Tests/Fixtures/golden/<name>.txt`.
4. Put these header lines at the top of the file (each starts with `# `):

```
# workspace: samples/showcase/showcase.grws
# config: Debug
# target: TMS320C6747
# root: C:\Users\GRA\work\GRIDEd\samples\showcase
```

- `workspace` is the `.grws`, `.pro` or `.sln`, relative to `Tests/Fixtures`; the workspace's
  whole folder is copied to a temporary folder before GRIDEm builds it.
- `root` is the workspace's folder on the Windows box, as GRIDEd's lines name it.

## What is compared

Only the progress lines - lines that start with `[tool]` - in order. Before comparing, both
sides are normalised:

- line ends: `\r\n` -> `\n`;
- `\` -> `/`;
- the `root` folder (GRIDEd) and the temporary copy's folder (GRIDEm) -> `$(Root)`;
- `.exe` dropped from program names;
- runs of blanks -> one blank.

The test is skipped (not failed) when GRIDEm's build reports a tool it cannot find, since
the toolchain then is not installed on this Mac (`make app` copies it into the bundle;
`GRIDEm-toolcheck` lists what is found).
