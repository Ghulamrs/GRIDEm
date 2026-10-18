# GRIDEm: GRIDE native on macOS (design)

> **Agreed reference, 2026-10-18.** This document is the contract for GRIDEm between G. R. Akhtar and Claude. The implementation language is **C++14 only** (`-std=c++14 -pedantic -Werror`), written in the plain style of section 0a. The AppKit UI is Objective-C++ under the same rule. A change to this document is agreed before it is made.

GRIDEm is the macOS member of the GRIDE line: an AppKit IDE that is GRIDEd's equal, built for the Mac rather than translated from WinForms.

It follows the design GRIDEd started from: `GRIDEd/docs/GRIDE-Design.zip` in commit `7e17c2a`, "Base version". It also follows what GRIDEd has grown into since: `GRIDEd/docs/ARCHITECTURE.md`, `TOOLCHAIN-FORMAT.md` and `README.md` at HEAD.

GRIDEm lives at `/Users/g.r.akhtar/Developer/Claude/GRIDEm`, beside GRIDE1 rather than inside it. It takes the toolchain from GRIDE1's `toolchain/` (and the tool repositories under `Developer/Claude`) without changing them. Where it goes in git is decided later.

---

## 0. Stance

| Question | Decision | Why |
|---|---|---|
| What is the reference? | GRIDEd's class map and its files (`.pro`, `.grws`, `gride.settings.json`, `toolchain.json`) | A workspace made on Windows opens on the Mac, and the other way round. Parity can be checked class by class. |
| UI technology | AppKit in Objective-C++ (`.mm`) | This was the choice for RIDE's Mac window too. It talks to C++ directly and avoids a Swift/C++ bridge. |
| The non-UI layers (Core, Model, Settings, Services) | **A fresh, portable C++14 library**, ported from GRIDEd's C++/CLI one class at a time, with the same names and behaviour | GRIDEd's logic is about 13,900 lines outside the UI. It is the reference, so porting it keeps the build commands, CCS reading and debugger dialogue identical. `System::String` becomes `std::string` (UTF-8), `List<>` becomes `std::vector`, events become callbacks. |
| RIDE's core (`GRIDE 1.0/src`) | Read as the original source of what GRIDEd ported (`toolchain.cpp`, `debugger.cpp`, `ccs/`, `project.cpp`'s VS reader), **not linked** | GRIDEd has moved on from it: `toolchain.json`, `.grws`, VS solutions with `cl`, `.dwtag` locals on the C6747. Linking RIDE's core would give RIDE's design, not GRIDE's. |
| What exists only on Windows | Kept out of the Mac build behind a Platform layer, and reported in plain words where a user meets it | `VisualStudio.h`, the `cl` parts, `cdb`, job objects, the pseudo-console |
| Mac-native means | NSDocument, NSSplitViewController, NSOutlineView, NSToolbar, SF Symbols, the system's appearance, sheets, the Settings window, NSPrintOperation, FSEvents, `posix_spawn`/`forkpty`, GCD | Mac users meet the behaviour they expect: autosave, Versions, window tabs, Dark Mode, VoiceOver, full screen, ⌘ shortcuts. |

---

## 0a. Language: C++14, written plainly

All C++ in GRIDEm, in the library and in the `.mm` files, is compiled with `-std=c++14 -pedantic -Werror`. The compiler itself then refuses C++17 and later. C++14 is the standard; the style keeps to the part of it a reader can follow line by line. That is essentially C++11 plus `std::make_unique`.

| Use | Avoid, even where C++14 allows it |
|---|---|
| classes with plain members, constructors, destructors | template metaprogramming, SFINAE, `enable_if`, type traits |
| `std::string`, `std::vector`, `std::map`, `std::set` | variadic templates (only the library's own use them) |
| `std::unique_ptr` with `std::make_unique`; `std::shared_ptr` where ownership is shared | `std::move` tricks beyond the obvious; move constructors written by hand |
| range-based `for` | `auto`, except for an iterator whose type is spelled out nearby |
| `enum class`, `nullptr`, `override`, `= delete` for copying | `decltype`, `decltype(auto)`, trailing return types, functions with deduced return types |
| short lambdas for callbacks; `std::function` only at event boundaries | generic (`auto`-parameter) lambdas, init-capture, long lambdas (more than a few lines becomes a named function), `std::bind` |
| `std::thread` and `std::mutex` only inside `Platform/`; GCD in the `.mm` files | variable templates, `constexpr` beyond simple constants, `<future>`, atomics outside `Platform/` |

Each class sits in one `.h` and one `.cpp`, with a header comment saying what it is and which GRIDEd class it mirrors. Names follow GRIDEd's, so the two can be read side by side.

---

## 1. Folder layout

```
/Users/g.r.akhtar/Developer/Claude/GRIDEm/
  GRIDEm.xcodeproj           generated from the Makefile's lists (as macos/make-xcodeproj.py does)
  Makefile                   make / make run / make test / make app
  README.md
  docs/ARCHITECTURE.md       this class map, kept current
  Sources/
    Core/        C++14       Json, PathUtil, LanguageTarget, TextFile
    Platform/    C++14+ObjC  Process, Pty, JobGroup, FileWatch, Paths (replaces NativeMethods)
    Model/       C++14       GrideProject, BuildConfiguration, Workspace, CcsReading, CcsProjectReader,
                             VsSolution, VsProjectReader
    Settings/    C++14       GrideSettings, SettingsManager, ToolchainDefinitions, ToolchainSettings,
                             CompilerOptions
    Services/    C++14       RideToolchain, BuildEngine, MacroExpander, ToolRunner, ProgramRunner,
                             BuildMessage, CodeConverter, Debugger, DebugProcess, GdbDebugger,
                             ShalimarSession, ProjectTemplates, ToolVersions
    UI/          ObjC++      (section 4)
  Resources/     toolchain.json (Mac), Assets.xcassets (themes, icon), Credits.rtf, MainMenu is code-built
  Tests/         XCTest for Core..Services; parity fixtures (section 7)
```

The C++ layers build into a static library, `libgride.a`, with no AppKit in it. Only `Platform/` has `.mm` files. The tests link the library alone.

---

## 2. Non-UI layers: GRIDEd → GRIDEm

| Layer | GRIDEd (C++/CLI) | GRIDEm (C++14) | Notes for the Mac |
|---|---|---|---|
| Core | `JsonValue/Parser/Writer` | same names | keeps key order and `//` comments, as now |
| | `PathUtil` | same | `/` separators; `.pro` paths stay relative; reads `\` written on Windows |
| | `LanguageInfo`, `TargetInfo`, `ConfigName` | same | |
| | `TextFile` | same | encodings and line endings kept as found (CRLF files from Windows stay CRLF) |
| | `NativeMethods` | → `Platform/` | see below |
| Platform | job objects | `JobGroup`: a process group per tool, `killpg` on Cancel | "Cancelling a build stops every process it started" still holds |
| | `CreatePseudoConsole` | `Pty`: `forkpty` | for lldb / gdb-shaped sessions and the Terminal pane |
| | process start without `cmd.exe` | `Process`: `posix_spawn`, pipes, `dispatch_source` readers | |
| | `%APPDATA%`, beside `GRIDE.exe` | `Paths`: `~/Library/Application Support/GRIDE/`, `GRIDEm.app/Contents/{MacOS,Resources}` | a portable install is a `gride.settings.json` beside the app |
| | — | `FileWatch`: FSEvents | Files pane; reloading a source changed outside the IDE |
| Model | `GrideProject`, `ProjectFolder`, `CcsInfo`, `VsInfo` | same | the same `.pro` JSON, written byte-compatible |
| | `BuildConfiguration` | same | |
| | `Workspace` (`.grws`) | same | breakpoints keyed by relative path, so they carry across machines |
| | `CcsReading`, `CcsProjectReader` | same | TI's option tables from `Resources/lib/ccs/*.tsv` |
| | `VsSolution`, `VsProjectReader` | same | read and shown; builds use GRIDE's toolchain. A part set to `cl` is reported as "Microsoft's compiler: Windows only". |
| Settings | `GrideSettings` + `EnvironmentSettings`, `LanguageSettings`, `ConverterSettings`, `LayoutSettings`, `RecentList`, `ExternalTool` | same | the same `gride.settings.json`; Windows-only keys (`vcvars`) kept and written back untouched |
| | `ToolchainDefinitions` (+ Tool/Session/Library/CompilerOption definitions) | same | **the same `toolchain.json` format**, shipped plus user overlay, merged field by field |
| | `ToolchainSettings`, `CompilerOptionStore` | same | |
| | `SettingsManager` | same | |
| Services | `RideToolchain` | same | tool lookup: env var, tool folder, `Contents/MacOS`, installation, PATH; **tries each alias with `.exe` stripped** |
| | `BuildEngine` | same | worker thread becomes a serial GCD queue; events go to the main queue |
| | `MacroExpander`, `ToolRunner`, `ProgramRunner` | same | `ProgramRunner`'s input line works over a pty |
| | `BuildMessage`, `DiagnosticReader` | same | |
| | `CodeConverter` | same | |
| | `DebugEngine`, `PlaceholderDebugger`, `DebugTranscript`, `ElfImage`, `DebuggerChoice`, `ProcessDebugger` | same | **lldb** for `arm64-darwin`; **sim6747 / vm6747** for `tms6747`. `cdb` is "Windows only". |
| | `DebugProcess` | same over `Pty`/`Process` | |
| | `ShalimarSession` | same | |
| | `ProjectTemplates`, `ItemTemplate`, `ToolVersions` | same | |
| | `VisualStudio` | **left out** | `#if` stub that answers "not on this machine" |

### The host on the Mac

The shipped `toolchain.json` marks `x86_64-windows` as `"host": true`, because it was written for GRIDE.exe. GRIDEm ships `Resources/toolchain.json` with the same content, except that `arm64-darwin` (or `x86_64-darwin` on Intel) is the host. The Mac's `run` is `program` and its `debug` is `debugger` (lldb), and the program names carry no `.exe`.

The part both files share stays one source of truth. A small check (`Tests/toolchain-diff`) fails when the two files differ in anything but these host fields. This is GRIDE1's W13 idea, extended to three IDEs.

| Target | Build on the Mac | Run | Debug |
|---|---|---|---|
| macOS (`arm64-darwin`) | c90 / cpp11 / shalimar → `as` → `cc` (Xcode's) | the program, in Output with an input line | lldb |
| TMS320C6747 | c90 / cpp11 / shalimar → asm6x → lnk6x + RTS6x | sim6747 (`--run`), Emulate on vm6747, Verify both | sim6747 `--run --debug`, or vm6747 |
| Windows (`x86_64-windows`) | to `.exe` through masm + link, where those tools are built for the Mac; else to `-S` | — ("a Windows program") | — |
| Linux | to `-S` | — | — |

---

## 3. The window

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ ● ● ●  [◧]  ▶ ■ │ NavFilter ▸ Debug ▸ TMS320C6747 ▾ │ Building kalman.c ▓▓▓░ 3/5 │ [◨] │  NSToolbar
├────────────┬─────────────────────────────────────────────────────────┬───────────────┤
│ ▣ ⌕ ◇ ●    │ main.c │ kalman.c × │ guide.shl                          │ Properties    │
│ INS_Suite  │ NavFilter › src › kalman.c › kalman_update()             │ File          │
│ ▾ NavFilter│  12  ●   x = F * x;                                      │  kalman.c     │
│   ▾ src    │  13  ➜   P = F * P * Ft + Q;                             │  C90 · c90    │
│     main.c │  14      ...                                             │ Configuration │
│     kalman │                                                         │  Debug · C6747│
│ ▸ Guidance │                                                         │  -g -D_DEBUG=1│
│ ▸ ImuDriver│                                                         │ Breakpoints   │
│   (CCS)    │                                                         │  ☑ kalman.c:12│
├────────────┴──────────────────────────────┬──────────────────────────┴───────────────┤
│ Locals │ Watch │ Call Stack │ Registers │ Memory │ Disassembly ║ Output ▾ Debug │ Errors │ Find
│ Name     Value        Type     Address      ║ (gdb-shaped) stopped at kalman.c:13    │
│ ▸ P      {…}          mat4     0xC0001F20   ║ > _                         [input]    │
└───────────────────────────────────────────┴─────────────────────────────────────────┘
 Ready · 0 errors · 2 warnings                         C90 · Debug · TMS320C6747 · Ln 13, Col 5
```

GRIDEd's areas, said the Mac way:

| GRIDEd | GRIDEm | AppKit construct |
|---|---|---|
| Left dock: Project Explorer, Class View, Files | **Navigator** sidebar with a symbol-icon switcher: Workspace, Class View, Files, Find, Breakpoints | `NSSplitViewController` sidebar item, `NSOutlineView` in source-list style |
| Right dock: Properties, Breakpoints | **Inspector** (⌥⌘0): Properties sections built from `PropertyObjects` | inspector split item, `NSStackView` of `NSGridView` forms |
| Bottom dock: Output, Error List, Find Results, Terminal, debug panes | **Bottom area** (⇧⌘Y). Left: the debug variables (Locals, Watch, Call Stack, Registers, Memory, Disassembly). Right: Output (Build / Debug / Tools popup, input line), Errors, Find Results, Terminal | `NSSplitView` holding two `NSTabView`s with segmented headers |
| Float a pane / dock back | "Open in Separate Window" on any pane. Closing it docks the pane back. Window > Reset Layout. | `NSPanel` holding the same view controller |
| Toolbar with Config / Target combos | Toolbar: Run, Stop, a **scheme popup** (project ▸ configuration ▸ target), an activity view, and Navigator/Inspector toggles. The step buttons appear only while debugging. | `NSToolbar` (unified), `NSPopUpButton` with a submenu tree, a custom activity item |
| Status bar | Status line under the window | `NSView` bottom bar (the `contentBorderThickness` look) |
| Dark / Light / Blue themes | Follows **System**; Dark, Light or Blue can be chosen per window | `NSAppearance` plus named colours in `Assets.xcassets`; Blue is a custom colour set over Dark Aqua |
| `IconFactory` (drawn in code) | SF Symbols; GRIDE-specific ones (C6747 chip, Shalimar) drawn in code | `+[NSImage imageWithSystemSymbolName:]` |
| `StartPage` | Welcome window: New, Open, recent workspaces, the samples | `NSWindow` like Xcode's welcome |

### Menus and keys

GRIDEd's command ids are kept as the single command table, so a command keeps its id on both platforms.

| Menu | GRIDEd ids it carries | Mac additions |
|---|---|---|
| **GRIDEm** | Help.About, Tools.Options → **Settings… ⌘,** | Services, Hide, Quit |
| **File** | File.New/Open/OpenCcs/ImportCcs/ImportFolder/Save/SaveAll/SaveAs/Close/CloseWorkspace/ExportZip/Print/PageSetup | Open Recent, Revert To ▸ (Versions), Duplicate, Move To |
| **Edit** | Edit.* (Undo … Upper/Lower, Comment, Indent, Format, Tabify, Bookmarks) | Emoji & Symbols, Dictation (system) |
| **Find** | Edit.Find/Replace/FindInFiles/GoToLine | Use Selection for Find ⌘E, Jump to Definition |
| **View** | View.* (panes, LineNumbers, Whitespace, Zoom, FullScreen, Theme, Toolbar, StatusBar) | Show/Hide Navigator ⌘0, Inspector ⌥⌘0, Bottom Area ⇧⌘Y |
| **Project** | Project.* (AddNewItem, AddExisting, NewFolder, Rename, Remove, Exclude, SetStartup, Dependencies, BuildOrder, Properties, CompilerOptions, CcsMapping, RefreshCcs) | |
| **Build** | Build.* (Compile, BuildProject/Workspace, Rebuild, Clean, Batch, ConfigManager, AssembleOnly, LinkOnly, Cancel) | |
| **Debug** | Debug.* (Start, StartNoDebug, Stop, Restart, BreakAll, StepOver/Into/Out, RunToCursor, ToggleBreakpoint, DeleteAll, AddWatch, SetVariable, Up/DownStack, Emulate, Verify, LoadProgram, Win*) | |
| **Tools** | Tools.ToolchainCheck, CToShalimar, ShalimarToC, ExternalTools, Snippets, Keyboard, CommandPrompt → **Open in Terminal** | |
| **Window** | Window.* (Float, Dock, CloseAll, CloseAllButThis, PinTab, ResetLayout, SaveLayout, Split) | Minimize, Zoom, Merge All Windows (system) |
| **Help** | Help.ViewHelp, UserGuide, Ref* (C90, C++11, Shalimar, MASM/LINK, ASM6x/LNK6x, RTS6747, VM6747), Shortcuts, ReleaseNotes | Help search field (system) |

**Key sets**, chosen in Settings > Keyboard, as GRIDEd's `KeySets` are:

| Command | macOS (default) | Visual Studio | RIDE |
|---|---|---|---|
| Build project / workspace | ⌘B / ⇧⌘B | ⌃B / F7 | as RIDE's |
| Run / Debug | ⌃⌘R / ⌘R | ⌃F5 / F5 | as RIDE's |
| Stop | ⌘. | ⇧F5 | |
| Step over / into / out | F6 / F7 / F8 | F10 / F11 / ⇧F11 | |
| Toggle breakpoint | ⌘\ | F9 | |
| Run to cursor | ⌃⌘C | ⌃F10 | |
| Clean | ⇧⌘K | — | |
| Comment / Uncomment | ⌘/ | ⌃K ⌃C / ⌃K ⌃U (chords kept) | |

---

## 4. UI layer: GRIDEd → GRIDEm

| GRIDEd class | GRIDEm class (file) | Built on |
|---|---|---|
| `MainForm` (5 `.cpp` files) | `MainWindowController` (`MainWindow.mm`) with categories `+File`, `+Edit`, `+Build`, `+Debug`, `+Commands` (one `.mm` per menu group, as GRIDEd splits them) | `NSWindowController`, `NSSplitViewController` |
| `Command`, `CommandRegistry`, `KeyText`, `KeySets` | `GRCommand`, `GRCommandRegistry` (`Commands.mm`): builds the `NSMenu`, the toolbar items and key equivalents from one table; ⌃K chords through a local event monitor | `NSMenuItem`, `NSUserInterfaceValidations` |
| `Theme`, `ThemeColorTable`, `ThemeRenderer`, `ThemeManager` | `GRTheme` (`Theme.mm`) | `NSAppearance`, asset catalogue colours |
| `IconFactory` | `GRIcons` (`Icons.mm`) | SF Symbols, plus a few drawn |
| `TabStrip`, `StartPage` | `GRTabBar` (`TabBar.mm`); `GRWelcomeWindow` (`Welcome.mm`) | custom `NSView`; `NSWindow` |
| `EditorDocument` | `GRSourceDocument` (`SourceDocument.mm`) | **`NSDocument`**: autosave, Versions, encodings, "changed on disk" |
| `DocumentManager` | `GREditorArea` (`EditorArea.mm`): tabs, split editor, jump bar | `NSSplitView`, `NSPathControl` |
| `CodeEditor`, `CodeTextBox` | `GRCodeView` (`CodeView.mm`) | `NSTextView` on TextKit 2; no 1.5 MB colouring limit (colouring is incremental, off the main thread) |
| `LineGutter` | `GRGutter` (`Gutter.mm`): numbers, breakpoints (click, drag off to delete), the current-line arrow, bookmarks, error marks | `NSRulerView` |
| `SyntaxHighlighter` | `GRHighlighter` (`Highlighter.mm`): keywords from `LanguageSettings` | temporary attributes on the layout manager (colouring never enters undo) |
| `UndoManager` | the system's | `NSUndoManager` per document |
| `ToolPane`, `DockArea`, `FloatWindow` | `GRPane` (protocol), `GRPaneHost` (`PaneHost.mm`), `GRPaneWindow` | `NSViewController`, `NSTabView`, `NSPanel` |
| `ProjectExplorerPane` | `GRWorkspaceNavigator` (`WorkspaceNavigator.mm`): read-only CCS/VS projects greyed with a lock badge; drag and drop to move files | `NSOutlineView` (source list) |
| `ClassViewPane` | `GRClassNavigator` | `NSOutlineView` |
| `FilesPane` | `GRFilesNavigator` | `NSOutlineView` + FSEvents |
| `PropertiesPane`, `PropertyObjects` | `GRInspector` (`Inspector.mm`) built from the same property descriptors | `NSGridView` |
| `BreakpointsPane` | `GRBreakpointsNavigator` | `NSTableView` with checkboxes |
| `OutputPane` | `GROutputView` (`OutputView.mm`): Build / Debug / Tools popup, input line, ANSI colours | `NSTextView`, `NSTextField` |
| `ErrorListPane` | `GRIssuesView` (`IssuesView.mm`) | `NSTableView`; issues also shown inline in the editor |
| `FindResultsPane` | `GRFindResultsView` | `NSOutlineView` |
| `TerminalPane` | `GRTerminalView` (`TerminalView.mm`) | `forkpty` + a small VT100 reader |
| `WatchPane`, `LocalsPane` | `GRVariablesView` (`VariablesView.mm`): Name, Value, Type, Address; changed values marked; edit to set | `NSOutlineView` |
| `CallStackPane` | `GRStackView` | `NSTableView` (double-click selects the frame) |
| `RegistersPane` | `GRRegistersView` | `NSTableView` |
| `MemoryPane` | `GRMemoryView`: hex + ASCII, address field | `NSTableView` in a fixed-width font |
| `DisassemblyPane` | `GRDisassemblyView` | `NSTextView` with the current-PC arrow |
| `ThemedDialog`, `InputDialog`, `TextDialog` | `GRSheet` (`Sheet.mm`) | `beginSheet:` |
| `NewProjectDialog`, `NewItemDialog` | `GRNewProjectSheet`, `GRNewItemSheet`: template grid, language, targets | `NSCollectionView` |
| `ProjectPropertiesDialog`, `CompilerOptionsEditor` | `GRProjectSettingsWindow`: a sidebar of pages, rows built from `toolchain.json`'s `compilerOptions`, the command-line preview, greyed rows with the reason | `NSSplitViewController`, `NSGridView` |
| `ConfigurationManagerDialog`, `DependenciesDialog`, `BatchBuildDialog` | sheets with the same names | `NSTableView` |
| `OptionsDialog` + its pages | `GRSettingsWindow`: General, Editor, Fonts & Colours, Keyboard, Toolchains (per target), **Definitions (`toolchain.json`)**, RIDE Paths, Converters, CCS | `NSTabViewController` in toolbar style |
| `FindReplaceDialog`, `GoToLineDialog` | the Find bar in the editor (⌘F / ⌥⌘F); Go to Line ⌘L popover | `NSTextFinder`, `NSPopover` |
| `WindowsDialog`, `SnippetsDialog` | sheets | |
| `ConvertDialog`, `ToolchainCheckDialog`, `ExternalToolsDialog`, `ShortcutsDialog` | sheets; Toolchain Check shows "found / missing / where looked" per tool | `NSTableView` |
| `AboutDialog` | the standard About panel with `Credits.rtf` and the tool versions (`ToolVersions`) | `orderFrontStandardAboutPanelWithOptions:` |
| Print / Page Setup | native | `NSPrintOperation` |
| Help viewer | `GRHelpWindow` over `docs/*.html` | `WKWebView` |

**Threading rule** (from GRIDEd's `BeginInvoke`): services report on their own queues, and `GRSession` (`Session.mm`) moves every event onto the main queue. `GRSession` owns the workspace, settings, `BuildEngine`, and the debug engine. No view touches a service directly.

---

## 5. Main flows (GRIDEd's, on the Mac)

| Flow | Path |
|---|---|
| Open a workspace | `MainWindowController+File openURL:` → `Workspace::Load` → `GrideProject::Load` / `CcsProjectReader::Read` / `VsProjectReader::Read` → `GRSession setWorkspace` → navigators reload |
| Build (⌘B) | `+Build build:` → `BuildEngine::StartAsync` on a GCD queue → `RideToolchain` + `ToolRunner` per step → events → main queue → Output, Issues, gutter marks, toolbar activity |
| Run (⌃⌘R) | `ProgramRunner` (pty) in Output with an input line; on the C6747, `sim6747 --run`, Emulate on vm6747, Verify |
| Debug (⌘R) | build → `DebuggerChoice::For` → `ProcessDebugger` (lldb / sim6747 / vm6747) or `ShalimarSession` → `Stopped` → the editor opens on the line, the debug area refreshes; frame choice through `SelectFrame` |
| Breakpoints | one sorted set per file, shared by `GRGutter`, `Workspace` (saved in `.grws`) and the engine (sent again when toggled while stopped) |
| Settings | `GRSettingsWindow` edits a copy → OK → `SettingsManager::Save` → `applySettings` (theme, key set, editor options, paths) |
| The toolchain file | `SettingsManager::Load` → `ToolchainDefinitions::Load(shipped, user)` → `ToolchainSettings::ApplyDefaults`; problems go to Output (Tools) and Toolchain Check |

---

## 6. The app bundle

```
GRIDEm.app/Contents/
  MacOS/      GRIDEm, c90, cpp11, shalimar, c2s, asm6x, lnk6x, sim6747, vm6747, masm, link
  Resources/  toolchain.json, lib/ (rts6x-tms6747, shalimar runtimes, ccs/*.tsv), docs/, help/, samples/
```

- The tools come from `../GRIDE1/toolchain/`, built for macOS into GRIDEm's own build folder, so nothing is written inside GRIDE1. GRIDE1's copy of the toolchain leaves out the tools' Makefiles (`installer/macos/README.md`). They come back first, from the tool repositories under `Developer/Claude`.
- `installer/macos/build-pkg.sh` then packages GRIDEm.app instead of RIDE's editor. It is signed ad hoc to start with, and with a Developer ID once one is chosen.

---

## 7. Proving parity

1. **Unit tests (XCTest)** for every non-UI class, written as each class is ported: JSON round-trip, `.pro` / `.grws` byte-compatibility, CCS reading over `samples/INS_Suite/ImuDriver` and RIDE's CCS sample, the VS reader over `samples/cppparse`, the macro expander, and the diagnostic parser over captured MSVC / TI / GNU / shalimar lines.
2. **Golden build lines.** GRIDEd's Output pane names every program it runs and the file each one writes. Capture GRIDEd's Build output on the Windows box for INS_Suite, cppparse and RIDE's 12 sealed projects on the TMS320C6747 target. GRIDEm's `BuildEngine`, run headless (`GRIDEm-build` test tool), must produce the same lines, apart from path prefixes and `.exe`.
3. **Debugger transcripts.** Recorded sim6747 / vm6747 / lldb sessions are replayed into `DebugTranscript`. The stops, locals, frames and watches must read the same as GRIDEd reads them.
4. **UI checklist**: one line per GRIDEd command id, marked done when it works on the Mac.

Following the house rule, each fix is its own commit with its own regression test.

---

## 8. Phases

| # | Phase | What is delivered | Done when |
|---|---|---|---|
| 0 | Ground | `GRIDEm/` folder, Makefile, generated Xcode project, empty app; the toolchain built for macOS into `Contents/MacOS` | `make app` gives a running empty window; `c90 --version` … from inside the bundle |
| 1 | Core + Settings | Json, PathUtil, LanguageTarget, TextFile, Platform, ToolchainDefinitions, ToolchainSettings, CompilerOptions, GrideSettings, SettingsManager; the Mac `toolchain.json` | tests pass; Toolchain Check (as a console tool) lists what it found, as GRIDEd's dialog does |
| 2 | Model | GrideProject, BuildConfiguration, Workspace, CcsReading, CcsProjectReader, VsSolution, VsProjectReader | INS_Suite, cppparse and the CCS samples load; `.pro`/`.grws` round-trip byte-equal |
| 3 | Build services | MacroExpander, ToolRunner, RideToolchain, BuildMessage, BuildEngine, ProgramRunner, CodeConverter, ProjectTemplates, ToolVersions | golden build lines match (section 7.2); C6747 programs run on sim6747 from the test tool |
| 4 | The window | MainWindowController and its categories, the command table, menus, toolbar, Navigator, editor (NSDocument, CodeView, Gutter, Highlighter, TabBar), Output, Issues, Find, Inspector, Welcome | open, edit, build, run, convert from the window; the UI checklist's File/Edit/View/Project/Build lines done |
| 5 | Debugging | Debugger, DebugProcess, GdbDebugger, ShalimarSession; the debug area (Variables, Stack, Registers, Memory, Disassembly), breakpoints, stepping, Run to Cursor, set variable | lldb on a macOS program and sim6747/vm6747 on a C6747 program, each with locals, watch, stack; transcript tests pass |
| 6 | Settings & dialogs | Settings window (all pages, Definitions editor), Project Settings with Compiler Options, Configuration Manager, Dependencies, Batch Build, Toolchain Check, External Tools, Snippets, Shortcuts, key sets, themes, pane windows, Terminal | the whole UI checklist done |
| 7 | Ship | Help window, About, print, `build-pkg.sh` producing a signed `.pkg`, README and user guide pages for the Mac | install on a clean account; samples build, run and debug |

Phases 1–3 need no UI and can be checked entirely by tests. Phase 4 starts only when the window has real services under it.

---

## 9. Decisions still open

1. ~~The name and folder~~ **Decided 2026-10-18:** `GRIDEm`, at `/Users/g.r.akhtar/Developer/Claude/GRIDEm`. Because it sits outside GRIDE1, GRIDE1's `CLAUDE.md` (the Mac is only the git bridge) needs no change.
2. Git: which repository GRIDEm goes into, decided later.
3. Should the C++ library later replace GRIDEd's non-UI C++/CLI as well, making one core for both? It would be possible (C++/CLI can call native C++), but it is not part of this plan.
4. Where masm and link (Windows targets) build and run on macOS, whether the Mac offers "Build for Windows" or stops at `-S`.
5. Apple Developer ID for signing and notarisation, or ad hoc only.
