# toolchain.json - the toolchain definition file of GRIDEd and GRIDE 2.0

The IDE keeps no tool name, library name or argument convention in its code: everything comes
from `toolchain.json`. Both IDEs read the same format, so one file serves both. The installer lays
the shipped copy down beside `GRIDE.exe`; the source of truth in GRIDEd's tree is
`GRIDEd\GRIDE\toolchain.json` (the project file copies it beside the exe on every build).

## Files and precedence

| file | role |
|---|---|
| `<folder of GRIDE.exe>\toolchain.json` | the shipped defaults; never written by the IDE |
| `%APPDATA%\GRIDE\toolchain.json` | the user's overrides (normal install) |
| `<folder of GRIDE.exe>\toolchain.user.json` | the user's overrides of a portable install (a `gride.settings.json` beside the exe) |
| `$GRIDE_TOOLCHAIN` | names the shipped file outright (tests, unusual layouts) |

The shipped file is looked for beside the exe, then (a source-tree layout) up to four folders above
it as `toolchain.json` or `GRIDE\toolchain.json`. The user's file is laid over the shipped one **field
by field**: an object merges recursively, every other value (string, number, bool, list) replaces the
shipped one whole; a field the user's file does not name keeps the shipped value; a field the IDE does
not know is kept as it is and written back unchanged. Tools > Options > Toolchains > Definitions
writes only what differs from the shipped file; "Restore shipped defaults" removes the entry.

The reader accepts `//` line comments (the shipped file uses them for its header); the user's file is
rewritten by the Options dialog without comments, so every entry also carries a `description`.

## Validation

Both files are checked at load. A field of the wrong kind is dropped with a message naming the file,
the entry and the field, and the other file's value (or the default) stands:

    toolchain.json (user, C:\Users\me\AppData\Roaming\GRIDE\toolchain.json): tool 'c90': field 'exe' must be a string - it is a number and is ignored (the other file's value, or the default, stands)

A required field missing from the shipped file is reported (`tool 'c90': field 'exe' is missing - the
tool has no program name and cannot be found`; `operation 'compileToAssembly': field 'args' is
missing`; `target 'tms6747': field 'ideTarget' is missing`; `library 'rts6x': field 'files' is
missing`). A user file that is not JSON: `toolchain.json (user, <path>) cannot be read: <error> - the
shipped file is used alone`. A shipped file that is not there: `the shipped toolchain.json was not
found - looked for <path> (beside GRIDE.exe, or named by GRIDE_TOOLCHAIN); no tool is defined until it
is there` - the IDE still starts, with every tool name empty. A target role naming an undefined tool:
`target 'tms6747': role 'simulator' names a tool 'simx' that is not defined - the role is empty`. The
messages appear in the Output pane (Tools) at start-up and in Tools > Toolchain Check, which also says,
for every tool not found on disk, where it was looked for (`$C90 (not set), the tool folder ..., beside
GRIDE.exe (...), the installation's ..., PATH`).

## The document

```
{
  "format": 1,                 // the format version
  "description": "...",
  "installation": { ... },     // where the toolchain is installed
  "visualStudio": { ... },     // where Visual Studio's tools and the Windows SDK are, found without cmd.exe
  "ccs": { ... },              // reading CCS projects: TI's option definitions, which projects are built, TI's runtime names, TI's compiler directories
  "compilerOptions": { ... },  // RIDE's Compiler Options table: the dialog's rows and what each becomes on a command line
  "placeholders": { ... },     // the names an argument template may use, each described
  "targets": { "<arch>": { ... } },
  "tools": { "<id>": { ... } },
  "libraries": { "<id>": { ... } },
  "linkerCommandFiles": { "<id>": { ... } }
}
```

Every section may carry a `description` key, which is skipped as an entry.

### installation

| field | kind | meaning |
|---|---|---|
| `defaultRoots` | list | the roots tried when Tools > Options > Projects names none: `%ProgramFiles%\RIDE` ... (environment variables expanded) |
| `binDir` | string | the sub-folder of a root holding the programs (`bin`) |
| `markers` | list | files that prove a folder is the installation's bin (`c90.exe`, `cpp11.exe`); a root counts when a marker is in it or in its `binDir` |
| `includeDir`, `libDir` | string | the installation's header directories the compilers are given (see a tool's `installIncludeDirs`) |

### visualStudio

RIDE imports `vcvars64.bat`'s `set` output through cmd.exe, which a policy may block; the IDE takes the
same directories from the installation's layout, as this section describes it, and sets `INCLUDE`,
`LIB` and `PATH` for every tool run on the host target (a setup script named under Tools > Options
still wins; a tool whose lookup says `vcvars` is looked for in these folders).

| field | kind | meaning |
|---|---|---|
| `vswhere`, `vswhereArgs` | string | the program that names the newest installation and its arguments (`-latest -prerelease -products * -requires ... -property installationPath`) |
| `defaultRoots`, `editions` | list | tried when vswhere is not there: `<root>\<edition>` holding `msvcTools` |
| `vcvars` | string | the batch file under the installation, for a setup script |
| `msvcTools` | string | the folder of compiler versions (`VC\Tools\MSVC`); the newest version is taken, unless `msvcVersion` names one |
| `msvcVersion` | string, optional | the compiler version to take under `msvcTools`: `"14.44"` takes the newest `14.44.*` (Visual Studio 2022's v143 where 14.52 is the newest), a whole folder name (`"14.44.35207"`) that folder; empty or absent: the newest. `cl`, Microsoft's `link.exe` and the `INCLUDE` / `LIB` / `PATH` directories all follow it. A version not under `msvcTools` takes the newest, and Visual Studio's description (Tools > Toolchain Check) says so (GRIDEd R13) |
| `hostBin`, `lib`, `include` | string | under the version: the tools (`bin\Hostx64\x64`), the libraries (`lib\x64`), the headers |
| `llvmBin` | string | clang's folder (`VC\Tools\Llvm\x64\bin`) |
| `windowsKits.registryKey`, `.registryValue` | string | where the Windows SDK's root is registered (`HKLM`) |
| `windowsKits.roots` | list | the usual roots when the registry says nothing |
| `windowsKits.versionsUnder` | string | the folder whose version-named sub-folders say which SDK versions are installed (`Lib`) |
| `windowsKits.libDirs`, `.includeDirs`, `.binDir` | list / string | the directories per version, `$(Version)` filled in |
| `environment` | object | the variables set for a tool run, each described: `INCLUDE`, `LIB`, `PATH` (each is the directories joined with `;` followed by its old value) |

### ccs

Reading Code Composer Studio 7.4 / 5.5 projects for the C6747, as RIDE's `src/ccs` reads them. CCS
stores in `.cproject` only what differs from TI's definition of an option, so an unstored option's
meaning and an enumerated value's command-line spelling come from TI's own definitions, shipped as
one TSV per compiler major version (`lib\ccs\options-ccs74-C6000_8.2.tsv`, `options-ccs55-C6000_7.4.tsv`,
made from the XML CCS ships).

| field | kind | meaning |
|---|---|---|
| `definitionsDir` | string | the folder of the definition files, looked for beside GRIDE.exe, above it (`..\lib\ccs`, the source layout's `GRIDE\lib\ccs`) and under the installation's roots |
| `definitions` | object | compiler major version -> file name (`"8"`: the 8.2 file, `"7"`: the 7.4 file); another version takes the nearest |
| `deviceFamily`, `deviceVariant`, `siliconVersion` | string | the projects the toolchain builds: family `C6000`, a variant containing `C674`, the core `6740`; a project for anything else, a COFF one or a big-endian one is read all the same, with RIDE's refusal as a note (GRIDEd opens it; RIDE would refuse it) |
| `compilerDirs` | object | major version -> the compiler directory's name prefix under `<CCS root>\tools\compiler` (`ti-cgt-c6000_`, `c6000_`), for `${CG_TOOL_ROOT}` when the version is not installed |
| `tiRuntime.index` | string | TI's library index (`libc.a`), which lnk6x does not read |
| `tiRuntime.library`, `.fallback` | string | the runtime it stands for (`rts6740_elf_eh.lib`, else `rts6740_elf.lib`); with RTS6x the build links the configuration's RTS6x library for it (the `rts6x` library's aliases); with TI's compiler directory named (Tools > Options > Projects > RIDE Paths), TI's `library` where it is in the directory's `lib` or the named library directory, else the `fallback` |
| `tiCompiler.roots` | list | where TI's compiler directories are looked for when none is named (RIDE's `detectedTi`): `%SystemDrive%\ti` (environment variables expanded) |
| `tiCompiler.under` | list | the layouts under each root: `""` (the root itself), `ccs*\tools\compiler`, `ccs*\ccs\tools\compiler` (a trailing `*` names any folder starting so); a folder named as `compilerDirs` says and holding the native linker in `bin` counts, the newest version wins (`8.10` is newer than `8.2`) |
| `tiCompiler.bin`, `.lib` | string | under a compiler directory: the linker's folder, the runtime's |

### compilerOptions

RIDE's Compiler Options (`options.cpp`): the table Tools > Options > Projects > Compiler Options and
Project > Compiler Options draw, and what each row becomes on a compiler's command line. A compiler's
configuration flags (`$(ConfigFlags)`) are made from the options it lists (its `options` field, see
tools), in that order, from the project's store (`.pro`: `"options": { "debug": {...}, "release":
{...} }`, only the values that differ from the defaults); a compiler that lists no options takes its
`configFlags`. The settings' store (`gride.settings.json`: `compilerOptions`) is what a new project
starts with.

| field | kind | meaning |
|---|---|---|
| `tabs` | list | the dialog's tabs in order (`General`, `C (c90)`, `C++ (cpp11)`, `Shalimar`) |
| `options` | object | option id -> the row; the id is `<tool word>.<option>` (`c90.opt`, `shc.g`), `general.*` for the rows several compilers take; the order of the keys is the order each tab shows them |
| `options.<id>.tab`, `.label`, `.hint` | string | the dialog's tab, the control's text, the line under it (the tip) |
| `options.<id>.control` | string, required | `check` (the value is `1` / `0`), `choice` (one of `choices`), `text` (`;` between the entries) |
| `options.<id>.choices` | list | a choice's values; the first is the compiler's own default |
| `options.<id>.default` | string or object | one default for both configurations, or `{ "Debug": ..., "Release": ... }`; absent: the first choice, `0`, nothing |
| `options.<id>.flagOn`, `.flagOff` | string | a check's flag when the value is `1` / `0` (`-g`; `--no_compress` when compress is off) |
| `options.<id>.flag`, `.silent` | string | a choice's flag with `$(Value)` filled in (`-j$(Value)`, `$(Value)`); `silent` names the choice that writes nothing (`Auto`, `-O0`; absent: the first choice) |
| `options.<id>.flagEach` | string | a text list's flag per entry, `$(Item)` filled in (`-D$(Item)`); an entry holding a space is quoted |
| `options.<id>.environment` | string | the variable the compiler is given instead of a flag (`CPP11_DECLINES`): `1` when the check is on, empty otherwise |
| `options.<id>.needs`, `.unavailable` | string | when the option applies: `lineTable` (the compiler writes one for the target), `emulated` (the C6747 only), `notEmulated`; `unavailable` is the sentence the dialog shows for a target it does not apply to (`$(Arch)` filled in); an option that does not apply writes nothing |

### targets - keyed by the arch word the compilers take

| field | kind | meaning |
|---|---|---|
| `ideTarget` | string, required | the IDE's target name: `Windows`, `Linux`, `macOS`, `TMS320C6747` |
| `name`, `description` | string | shown in the Options pages |
| `host` | bool | the programs run on this machine |
| `emulated` | bool | the program runs on the emulator or simulator |
| `debugInfo` | string | what a Debug build carries: `codeview`, `dwarf`, `dwpsn`, or empty (no line table: `-g` is dropped) |
| `assemblyExtension`, `objectExtension`, `programExtension` | string | the files of a build (`.s`, `.obj`, `.exe` / `.out`) |
| `emulatedProgramExtension` | string | the emulator's program directory (`.vm`) |
| `libraryDirFlag`, `libraryFlag` | string | how a library directory and a library are spelled to the linker (`/LIBPATH:`, `-L` / `-l`, `-i ` / `-l `) |
| `roles` | object | role -> tool id for this target: `cCompiler`, `cppCompiler`, `shalimarCompiler`, `hostCompiler`, `assembler`, `linker`, `debugAssembler`, `debugLinker`, `nativeAssembler`, `nativeLinker` (the vendor's tools offered by the native-tools question when the project's own fail), `simulator`, `emulator`, `debugger` |
| `runtime` | string | the `libraries` entry linked as the C runtime (`rts6x`, `hostCrt`) |
| `linkerCommandFile` | string | the `linkerCommandFiles` entry written when the project brings none |
| `run` | string | how a program is started here: `program` (itself), `simulator` (the simulator's `simulate` operation), `none` (the target only reaches assembly on this machine) |
| `debug` | string | `debugger` (the debugger's `debug` operation), `simulator` (the simulator's `debug` operation), `none` |
| `shownRun`, `shownEmulate` | string | the one-line summary the Output pane prints for a run / an emulation (`$(Compiler) -S ... && $(Assembler) && $(Linker) ... && $(Simulator) ...`) |

### tools - keyed by a stable id the code uses

| field | kind | meaning |
|---|---|---|
| `name` | string | what the IDE prints (`c90`, `cl`, `link.exe`) |
| `description` | string | what the tool is and does |
| `role` | string, required | `cCompiler`, `cppCompiler`, `shalimarCompiler`, `hostCompiler`, `hostCxx`, `assembler`, `linker`, `debugAssembler`, `debugLinker`, `nativeAssembler`, `nativeLinker`, `simulator`, `emulator`, `debugger`, `fallbackDebugger` (stands in for the `debugger` when that one is not installed: lldb on Windows), `converter` |
| `languages` | list | what it compiles: `C`, `C++`, `Shalimar` |
| `targets` | list | the arch words it serves (empty: all) |
| `exe` | string, required | the program's file name (RIDE's actual name) |
| `aliases` | list | the other names tried, in order (the brief's `cc1.exe`, `shc.exe`, `ASM6x.exe` ...) |
| `words` | list | what a project file may call it (`c90`, `cc1`; `msvc`, `cl`, `c++`, `g++`, `clang++`) |
| `lookup.env` | string | the environment variable that names it outright (`C90`, `VM6747`, `RIDE_CDB`) |
| `lookup.installDir` | string | the installation sub-folder it lives in (`bin`), empty for a tool that is not the installation's |
| `lookup.dirs` | list | folders of its own, looked in after the ones above and before PATH; `%VAR%` is expanded (`cdb`: the Windows SDK's `Debuggers\x64`) |
| `lookup.path` | bool | also looked for on PATH (false for RIDE's `link` and `lnk6x`, which Microsoft's and TI's would be taken for) |
| `lookup.vcvars` | bool | Visual Studio's, reached through `vcvars64.bat` |
| `lookup.description` | string | the search in words |
| `version.args` | string | the arguments that print its version (`--version`); no `version` object: no query |
| `outputEncoding` | string | how its output is read: `utf-8` (RIDE's tools) or `oem` (the console's code page: cl, link.exe, cdb) |
| `diagnostics` | string | the shape of its error lines: `gnu` (`file:line:col: error: msg`), `msvc` (`file(line,col): error Cnnnn: msg`), `shalimar` (`Error: line N: msg`), `masm` (`file: line N: msg`), `ti`, `none` |
| `environment` | object | the variables the tool itself reads, each described (`C90_AS`, `C90_LD`, `SHM_DEBUG`, `RIDE_DEBUG_SIM6747`) |
| `archFlag` | string | the target as the tool spells it, `$(Arch)` filled in (`-arch $(Arch)`, `--target=$(Arch)`); empty for a tool that builds for the machine it is on |
| `includeFlag`, `defineFlag` | string | its `-I` / `-D` spellings (`/I`, `/D`); empty: the tool takes none (shalimar) |
| `installIncludeDirs` | list | the installation directories it is given as includes: `include`, `lib` |
| `languageFlags` | object | the switch per language (`C`: `/TC`, `C++`: `/TP /EHsc /std:c++14`) |
| `configFlags` | object | the flags per configuration: `Debug`, `Release` - RIDE's flags before the Compiler Options existed; what a tool that lists no `options` takes (cl, the host's C++) |
| `configFlagsByTarget` | object | arch -> { `Debug`, `Release` }: the exceptions (shalimar on tms6747: `-g` without `--debug`) |
| `options` | list | the `compilerOptions` ids the tool takes, in the order their flags are written (c90: `c90.opt`, `c90.g`, `c90.defines`, `c90.undefines`, `general.nologo`, `general.jobs`, `general.time`); the project's Compiler Options then make `$(ConfigFlags)` |
| `projectSettings` | object, optional | a Visual Studio project's own compiler settings and the switch the tool takes for each (GRIDEd R13; `cl` carries it): MSBuild's ClCompile name -> { value -> switch }, e.g. `"WarningLevel": { "Level4": "/W4" }`, `"RuntimeLibrary": { "MultiThreaded": "/MT", "MultiThreadedDebug": "/MTd" }`; `"*"` stands for any other value (`$(Value)` filled in), a switch of `""` reads the setting and gives nothing (`"MultiProcessorCompilation": { "*": "" }`); names and values in any case; a `description` key is allowed. When the tool compiles a project read from Visual Studio, the switches its configuration's settings map to go into `$(CompileFlags)` after the configuration's own flags, in this object's order, followed by the project's `AdditionalOptions`; the Output pane names them once (`[the project's own settings give cl: /W4 /WX /permissive- /EHsc /MT]`) and lists a setting the object does not name (`[not given to cl - ...]`) |
| `session` | object, or a tool id | for a tool that debugs, and for `shalimar`, whose Debug build stops itself: how the child is driven on its standard input (below); a tool id names another tool's session to start from |

#### session - how a debugger is driven

RIDE asks a debugger one command at a time and then for a marker, reading everything up to the
marker as the answer. The session says how, per debugger; what the answers mean is read by the
`dialect`'s rules in the code (gdb's `Breakpoint 1, main () at f.c:11`, lldb's `frame #0: ...`,
cdb's `f.c(11)` and `module!function`). The `shalimar` dialect is the other way round: the
program itself talks, on its standard error, in lines that start with the `protocol` words.

| field | kind | meaning |
|---|---|---|
| `dialect` | string | `gdb`, `lldb` or `cdb`: the shapes the answers are read in. The C6747 emulator and simulator answer in gdb's shapes. `shalimar`: the program's own session (RIDE's `shalimar::Session`) |
| `like` | string | the tool whose session this one starts from (`vm6747` and `sim6747` start from `gdb`'s, `lldbWindows` from `lldb`'s); its own fields are laid over it |
| `console` | bool | the child is started on a pseudo-console (RIDE's `startOnConsole`): cdb flushes its prompt only to a console. The console's escape sequences and its echo of what was typed are taken out as it is read; Enter is `\r` |
| `environment` | object | NAME -> value the child starts with (`SHM_DEBUG`: `1` for the Shalimar session) |
| `preamble` | list | the commands sent once at start (`set confirm off` ...; cdb's `.lines -e`, `l+t`, `l+s`, `n 10`, `.step_filter`) |
| `marker` | string | the text that ends every answer (`<<gride-done>>`); none for the `shalimar` dialect, whose lines are whole |
| `markerCommand` | string | the command that prints it (`echo <<gride-done>>\n`; cdb's `.printf` and lldb's `script print` in pieces, so the echo of the command is not the marker) |
| `commands` | object | the spelling of each thing the IDE asks: `break`, `clearBreakpoints`, `run`, `continue`, `stepOver`, `stepInto`, `stepOut`, `runToLine`, `args`, `locals`, `evaluate`, `address`, `type`, `set`, `frames`, `frame`, `registers`, `memory`, `afterMove` and `where` (cdb's `.lastevent` and `ln` after a move), `quit` - with `$(File)` (the file's leaf name), `$(Line)`, `$(Expr)`, `$(Name)`, `$(Value)`, `$(N)`, `$(Count)`, `$(Address)` filled in; a command the session leaves out is not asked. The `shalimar` dialect's: `break` and `clearBreakpoint` with `$(Unit)` and `$(Line)` (`b 0 8`, `d 0 8`), `run`, `continue`, `stepOver`, `stepInto`, `stepOut`, `where`, `quit` |
| `protocol` | object | the `shalimar` dialect's line heads: `file` (`#file <unit> <leaf>`, one per source), `ready` (`#ready`), `stop` (`#stop <unit> <line> <depth>`), `at` (`#at ...`, the answer to `where`), `exit` (`#exit <status>`) |
| `afterReady` | string | the command id sent once after the ready line (`where`): the shipped Shalimar runtime reads and discards the first line after `#ready`, and a `where` costs nothing either way |

Where the Shalimar session runs: the program itself on a host, with the session's environment;
on tms6747 its `.out` under the simulator's `session` operation (`sim6747 --run "<out>"`, RIDE's
`sessionCommand`), which carries `SHM_DEBUG` to the program. It is chosen only where no debugger
reads the program (RIDE's `dbg_planFor`): cdb reads a Windows Debug build's CodeView, the emulator
and simulator read a tms6747 build's `.dwpsn` lines, so the session is for a Windows without cdb
and a tms6747 without `vm6747`.
| `asmSyntaxFlags.codeview` | string | added to a Debug build whose target carries CodeView (`-masm=gnu`) |
| `asmSyntaxFlags.assemblerNamed` | string | added to a host build when an assembler is named (`-masm=masm`) |
| `operations` | object | operation id -> { `description`, `args` (required), `shown`, `output`, `arguments` } |

The operations and what the IDE uses them for:

| operation | tools | use |
|---|---|---|
| `compileToAssembly` | the compilers | every build's first step; also the Compile File command and the Disassembly pane. shalimar's takes `$(Sources)`: a Shalimar program is one compilation of every `.shl` of the build, the assembly named after the program |
| `compileToObject` | the compilers | a project of several compilers, objects for the host link |
| `compileAndLink` | the compilers | a single file to a program (the compiler links itself) |
| `assemble` | assembler, debugAssembler, nativeAssembler | one assembly file to one object |
| `link` | linker, debugLinker, nativeLinker | the objects to the program |
| `simulate`, `debug`, `session` | simulator | run the `.out`; the debugging session; a Shalimar program's `.out` run for its own session |
| `emulate`, `launch`, `debug` | emulator | Build > Emulate; a `.s`/`.vm` with its arguments; the debugging session |
| `debug` | debugger, fallbackDebugger | start the program under cdb / gdb / lldb |
| `convertToShalimar`, `convertToC` | converter | Tools > Convert |

`args` is the argument template after the program (the IDE quotes the program itself); `shown` is the
short line printed in the Output pane when it differs; `output` says what file the operation writes;
`arguments` explains every argument (`"-S": "stop after the assembly"`). The templates become the
per-target command templates of Tools > Options > Toolchains (`"$(Compiler)" ` + `args`), where the
user may still change them per target; a template still at its default follows the definitions.

### Placeholders

`$(Compiler) $(Assembler) $(Linker) $(DebugAssembler) $(DebugLinker) $(Simulator) $(Emulator)
$(Debugger) $(Runtime) $(RuntimeDir) $(ToolDir) $(Source) $(Sources) $(SourceDir) $(Name) $(AsmFile) $(AsmFiles)
$(ObjFile) $(Objects) $(Output) $(Program) $(ObjDir) $(OutDir) $(Arch) $(ArchFlag) $(ConfigFlags)
$(AsmSyntaxFlag) $(LangFlags) $(IncludeFlags) $(DefineFlags) $(LibDirFlags) $(LibFlags) $(CrtLibs)
$(Optimise) $(CompileFlags) $(LinkFlags) $(LinkerCmd) $(Config) $(Target) $(ProjectDir) $(ProjectName)
$(WorkspaceDir) $(Arguments) $(ShalimarRuntimeDir)` - each explained in the file's `placeholders` section.
A path placeholder is written without quotes; the template quotes it where the tool wants quotes.

### libraries - keyed by id

| field | kind | meaning |
|---|---|---|
| `name`, `description` | string | shown in the Options pages |
| `kind` | string | `runtime` (the C runtime), `printf`, `shalimarRuntime` (the C6747's `shmrt6x`, linked before the C runtime and taken as assembly by the emulator; the Windows one, `lib\shmrt-x86_64-windows[-debug].lib`, which RIDE's `shalimar` links itself and GRIDE links first in a Shalimar program's host link - the Debug file is the runtime with the session in it), `hostCrt` |
| `target` | string | the arch it belongs to |
| `directory` | string | the installation-relative directory holding the files (`lib\rts6x-tms6747`), looked for under the tool folder, beside GRIDE.exe and the installation, in that order |
| `directoryEnv` | string | the environment variable naming the directory outright (`RTS6X`) |
| `marker` | string | the file that proves the directory is the right one (`rts6x.lib`) |
| `files` | object, required | `Debug` / `Release` -> the library file(s), a space-separated list where the link takes several (the host CRT) |
| `aliases` | list | the names a project file may use for it, each read as the configuration's file; `*` at the end matches any ending (`rts6740*`) |
| `assemblyDirectories` | object | `Debug` / `Release` -> the runtime's assembly directory for the emulator (`lib\shmrt-tms6747-debug`) |
| `assemblyDirectoryEnv` | object | `Debug` / `Release` -> the environment variable naming that directory (`SHMRT6747D`) |
| `when` | string | when it is linked: `always`, `shalimar` (a Shalimar program), `printf`, `never` |

### linkerCommandFiles - keyed by id

| field | kind | meaning |
|---|---|---|
| `name` | string | the file name written into the build directory (`ti-link.cmd`) |
| `description` | string | |
| `lines` | list | the file's text, one line per entry |

## GRIDE 2.0

GRIDE 2.0 reads the same file with the same reader (`src\Toolchain\ToolchainFile.h`, a port of GRIDEd's
`ToolchainDefinitions.h`; `src\Common\Json.h` reads, merges and writes the document). Its files:

| file | role |
|---|---|
| `<folder of GRIDE.exe>\toolchain.json` | the shipped defaults (the project copies `GRIDE\toolchain.json` beside the exe on every build) |
| `<folder of GRIDE.exe>\toolchain.user.json` | the user's overrides when this file exists (a portable layout) |
| `%APPDATA%\GRIDE\toolchain.json` | the user's overrides otherwise - the file GRIDEd reads too, so one set of overrides serves both IDEs |
| `$GRIDE_TOOLCHAIN` | names the shipped file outright |

The shipped file is looked for beside the exe, then up to four folders above it as `toolchain.json` or
`GRIDE\toolchain.json` (the source layout `GRIDE\x64\Debug\GRIDE.exe`). The validation and its messages
are the ones above.

GRIDE 2.0 keeps one project at a time and a flat list of tools in its settings (Tools > Options >
Toolchains: one entry per thing the IDE runs, with a program and an argument template). Every entry is
made from the file through a fixed binding of the IDE's tool ids to the file's roles and operations:

| settings entry | from the file |
|---|---|
| c90, cpp11, shalimar | the tools of the roles `cCompiler`, `cppCompiler`, `shalimarCompiler`; operation `compileToAssembly` |
| masm, link (Windows) | `targets.x86_64-windows.roles.assembler` / `.linker`; operations `assemble` / `link`; the linker's library switch is the target's `libraryFlag` |
| clang, link.exe (Windows) | the roles `debugAssembler` / `debugLinker` (a Debug build's CodeView path); operations `assemble` / `link` |
| ml64 (Windows) | the role `nativeAssembler`; operation `assemble`. The target's `nativeLinker` (mslink) is the `debugLinker` entry |
| as, cc (Linux, macOS) | the same roles of `x86_64-linux` / `arm64-darwin` |
| asm6x, lnk6x (TMS6747) | the same roles of `tms6747` |
| TI's lnk6x (TMS6747) | the role `nativeLinker`; operation `link`; looked for in TI's C6000 compiler directory only (`ccs.tiCompiler`) |
| RTS6x | the library `targets.tms6747.runtime` names: its Release file is the entry's file, its Debug file the alias, `directory` / `directoryEnv` the lookup |
| vm6747, sim6747 | the roles `emulator` (operation `launch`) and `simulator` (operation `simulate`) |
| c2s (both ways) | the tool of role `converter`; operations `convertToShalimar` / `convertToC` |
| Linux Runner, macOS Runner | not in the file: the program itself on that machine |

The entry takes the tool's `exe` and `aliases`, the operation's `args` as its argument template (the
file's `$(Name)` placeholders; GRIDE 2.0's older `{name}` spellings are filled in as well, and Help >
Placeholder Reference lists both), `includeFlag` / `defineFlag` as its switches, `lookup` as where it is
looked for, `outputEncoding` as its code page (`utf-8` is 65001, `oem` is 0). The target table takes each
target's `ideTarget`, arch word, extensions, `run`, `runtime` and `linkerCommandFile`; the build engine
takes a tool's `configFlags` / `configFlagsByTarget`, the host CRT from the `hostCrt` library, the Debug
runtime from the runtime library's `files`, and writes the target's linker command file into `obj` when the
project brings none. The `installation` section gives the default roots, the `bin` folder and the markers
RIDE's installation is known by. A tool whose `lookup` has no `installDir` (ml64, clang, link.exe, TI's
lnk6x, as, cc) is not looked for beside `GRIDE.exe` or in the installation, so RIDE's own `link` and
`lnk6x` are never taken for Microsoft's or TI's.

The build engine (R04) also takes: a compiler's `asmSyntaxFlags` (`codeview` for a Debug build on a
target whose `debugInfo` is `codeview`, `assemblerNamed` for the host target with an assembler named) as
`$(AsmSyntaxFlag)`; the compilers' `environment` entries ending in `_AS` / `_LD`, set before a compiler
runs to the target's assembler and linker (empty for a CodeView build); `installIncludeDirs` with the
installation's `includeDir` / `libDir`, given after the project's include folders; a target's `host`
(Visual Studio's variables, another machine's target only reaching assembly) and `emulated`; the
`visualStudio` section, whose `environment` names the variables every host tool runs with (`INCLUDE`,
`LIB`, `PATH`, found without `cmd.exe`) and whose layout finds the `vcvars` tools; `ccs.tiCompiler`
(`roots`, `under`, `bin`, `lib`) with `ccs.compilerDirs` for TI's compiler directory; the runtime
library's `aliases` (a project's `libc.a`, `rts6740*` ... read as the configuration's RTS6x); the
`shalimarRuntime` library of the target (its `files` linked before the runtime for a Shalimar program, its
`assemblyDirectories` / `assemblyDirectoryEnv` assembled into `obj\shmrt` where no file is packed); the
emulator's name in `[no .out: ... Run is Emulate on vm6747]`.

An entry follows the file until its program, arguments or switches are changed under Tools > Options,
which marks it `custom` in `settings.json` (settings version 5; an older file's entries are marked by
comparing them with the earlier defaults); "Restore from toolchain.json" takes the file's definition
back. The user's `toolchain.json` is hand-edited: Tools > Options > Definitions shows the two files, every
problem the reader found, the file's tools and libraries with the overridden ones marked, and offers
"Edit user overrides..." (makes the file with its header and opens it in the editor), "Reload" and
"Discard user overrides"; Tools > Check Toolchain prints the same to the Output pane with where every
tool was found or looked for, and the problems are printed there at start-up.

Not used yet by GRIDE 2.0 (the revisions after R04 take them up): the rest of `ccs` (R06),
`compilerOptions` (R07), the debuggers' `session` entries (R09). GRIDEd's R13 fields - a tool's
`projectSettings` and `visualStudio.msvcVersion` - are kept by GRIDE 2.0's reader as unknown fields and
not used. GRIDE 2.0's four targets are its own
enumeration: the file's entries for them give the words, extensions and tools; a fifth target in the file
is read but has no place in the IDE. `$(LinkerCmd)` is the first command file of the build, unquoted (the
project's, else the written one); `{cmdfiles}` is every one, quoted. `$(Sources)` is every `.shl` of a
Shalimar program, each quoted, which shalimar compiles as one (the one source of any other compile).

## What stays in the code

The roles, operation ids, placeholder names, the IDE's target names and the language words (`C`,
`C++`, `Shalimar`) are the format's vocabulary, so the code knows them. The sentences the IDE prints
are the IDE's (they take the names from the file). The defaults of GRIDEd's first release
(`grc90.exe`, `C:\GRIDE\bin\...`), which an old `gride.settings.json` may still hold, are kept as
migration data so such a file follows the port.

## Changing the toolchain

- A renamed program: change the tool's `exe` (and `aliases`) in the user's file or under
  Toolchains > Definitions; every command line, search and message follows.
- A changed switch: change the operation's `args`; a changed configuration flag: the `compilerOptions`
  row's `flagOn` / `flag` / `flagEach` (or `configFlags` for a tool that lists no options).
- A new compiler option: add a `compilerOptions.options` row and name its id in the compiler's
  `options` list where its flag belongs; the dialogs, the `.pro` and the command lines follow.
- A debugger that shows its prompt only on a console (as cdb does): `session.console: true`; one
  that needs a variable set: `session.environment`. A Shalimar runtime that spells its protocol
  lines or its commands differently: `session.protocol` and `session.commands` under `shalimar`.
- A debugger that spells a command differently: change the entry in its `session.commands` (or
  the one of the tool it is `like`, which it then follows).
- A new library: add a `libraries` entry and name it in the target's `runtime` or in the project.
- The installer agent lays down the shipped file; the user's file may be shipped as well as a
  starting point, since unknown fields and partial entries are both fine.
