# GRIDEm - GRIDE native on macOS

GRIDEm is the macOS member of the GRIDE line: an AppKit IDE for C90, C++11 and Shalimar on
macOS, Windows, Linux and the TMS320C6747, the equal of GRIDEd (GRIDE1's Windows IDE), built for
the Mac rather than translated from WinForms. It reads and writes the same files as GRIDEd -
`.pro`, `.grws`, `gride.settings.json`, `toolchain.json` - so a workspace moves between the two.

**The agreed reference is [`docs/GRIDEm-Design.md`](docs/GRIDEm-Design.md)**: C++14 only
(`-std=c++14 -pedantic -Werror`), in the plain style of its section 0a; the UI is Objective-C++
under the same rule. [`docs/PORTING.md`](docs/PORTING.md) holds the working rules every ported
file follows; [`docs/HANDOVER.md`](docs/HANDOVER.md) is the state of the work and what the first
build on the Mac has to do.

## Layout

```
Sources/Core       C++14   Json, StringUtil, PathUtil, LanguageTarget, TextFile
Sources/Platform   C++14   Paths, Environment, CommandLine, Process, Pty, JobGroup; FileWatch.mm (FSEvents)
Sources/Settings   C++14   ToolchainDefinitions, CompilerOptions, ToolchainSettings, GrideSettings (+ SettingsManager)
Sources/Model      C++14   BuildConfiguration, VsSolution, CcsReading, GrideProject, CcsProjectReader,
                           VsProjectReader, Workspace, XmlReader
Sources/Services   C++14   RideToolchain, VisualStudio (Windows only), BuildMessage, MacroExpander, ToolRunner,
                           ToolVersions, ProgramRunner, CodeConverter, BuildEngine, ProjectTemplates,
                           Debugger, DebugProcess, GdbDebugger, ShalimarSession
Sources/UI         ObjC++  AppDelegate, Session, MainWindow (+File/+Edit/+Build/+Debug/+Commands), Commands,
                           Theme, Icons, HelpWindow, Welcome, Editor/, Panes/, Dialogs/
Resources                  Info.plist, toolchain.json (the Mac as host), lib/ccs, docs, samples, Credits.rtf
Tests                      XCTest (Core, Platform, Settings, Model, Services, Parity) and Fixtures/
Tools                      GRIDEm-build, GRIDEm-toolcheck, build-toolchain.sh, make-xcodeproj.py, toolchain-diff.py
```

The C++ layers build into `build/libGRIDEm.a` with no AppKit in it; they compile and run on any
POSIX box. The class names follow GRIDEd's, one GRIDEd file to one `.h` + `.cpp`.

## Build

```
Tools/build-toolchain.sh   # the eleven tools for this Mac, from ../GRIDE1/toolchain, into build/toolchain
make                       # build/libGRIDEm.a and build/GRIDEm.app (the tools copied into it)
make run                   # build and open the app
make tools                 # build/GRIDEm-build (headless builds) and build/GRIDEm-toolcheck
make check                 # every C++ source compiled alone under the contract's flags
make xcodeproj             # GRIDEm.xcodeproj (libGRIDEm, GRIDEm, GRIDEmTests)
make test                  # the XCTest suite through xcodebuild
python3 Tools/toolchain-diff.py   # Resources/toolchain.json agrees with GRIDEd's, apart from the host
```

## Files GRIDEm uses

| File | Where |
|---|---|
| `toolchain.json` (shipped) | `GRIDEm.app/Contents/Resources` |
| `toolchain.json` (your overrides) | `~/Library/Application Support/GRIDE/` (portable: `toolchain.user.json` beside the app) |
| `gride.settings.json` | `~/Library/Application Support/GRIDE/` (portable: beside the app) |
| `*.pro`, `*.grws` | your projects, as GRIDEd writes them |
