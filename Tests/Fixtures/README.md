# Recorded debugger sessions (reference section 7.3)

The replay tests feed recorded sessions into `DebugTranscript` and check that the stops,
locals, frames, watches, registers and memory read the same as GRIDEd reads them.

## Layout

```
Tests/Fixtures/
  sessions/
    <backend>/                       lldb, sim6747, vm6747 (and cdb, recorded on Windows)
      <program>-<what>.transcript    the session as DebugProcess records it
      <program>-<what>.expected.json what GRIDEd read from it
  projects/                          the small programs the sessions debug (sources only)
```

`<backend>` is the debugger, `<program>` the sample (`sieve`, `shapes`, `factorial`, `SampleExt` ...),
`<what>` the scenario (`break-step`, `locals`, `frames`, `watch`, `registers`, `memory`, `exit`).

## The .transcript format

What `DebugProcess::Transcript()` holds (and GRIDEd's `ProcessDebugger::Said` with the commands):

- a line `> command` is what the IDE said - the command, then the marker command;
- every other line is what came back, until the next `> ` line;
- a line `! text` was heard on standard error (a session started apart);
- the marker (`<<gride-done>>` for lldb, the session's own for the others) stays in the
  answer, as the readers expect it.

`DebugTranscript::LoadRecorded(path, exchanges, error)` reads a file into
`DebugExchange { Command, Answer }` pairs; `ReadRecorded(text)` does the same from text.
A test takes each answer to the reader its command implies (`locals` -> `ReadVariables`,
`frames` -> `ReadFrames`, a move -> `ReadStop` and `ProgramOutput`, `registers` ->
`ReadRegisters`, `memory` -> `ReadMemoryBytes`, `evaluate` -> `ReadValue`).

## The .expected.json format

```json
{
  "dialect": "lldb",
  "marker": "<<gride-done>>",
  "reads": [
    { "command": "next", "reader": "stop",   "expect": { "stopped": true, "file": "sieve.c", "line": 12, "function": "main" } },
    { "command": "frame variable -L -T", "reader": "locals",
      "expect": [ { "name": "i", "value": "3", "type": "int", "address": "0x16fdff2ac" } ] }
  ]
}
```

## Recording one

- **lldb / sim6747 / vm6747** - on the Mac: debug the sample in GRIDEm, then save the
  session's transcript (the Output pane's Debug source keeps it) into `sessions/<backend>/`.
- **GRIDEd's reading** - on the Windows box: run the same session in GRIDEd and write what
  its `DebugTranscript` read into the `.expected.json`; that file is the parity oracle.
- **cdb** - recorded on Windows only; the reader is kept in GRIDEm so these still replay.
