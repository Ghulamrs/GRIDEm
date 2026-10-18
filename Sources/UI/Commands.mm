// =============================================================================
//  Commands.mm - see Commands.h
// =============================================================================
#import "Commands.h"
#import "Common.h"
#import "Icons.h"

const NSInteger kGRRecentFilesTag = 9101;
const NSInteger kGRRecentWorkspacesTag = 9102;
const NSInteger kGRToolbarsMenuTag = 9103;
const NSInteger kGRExternalToolsTag = 9104;

// -----------------------------------------------------------------------------
//  The table. Columns:
//    id, title, category, SF Symbol,
//    mac  - the Mac's key ("" none)
//    vs   - Visual Studio's (GRIDEd's own default); nullptr: as mac
//    ride - RIDE's (GRIDEd's KeySets::For("ride")); nullptr: as vs
//    responder - a selector sent down the responder chain instead of a handler
//    tag  - the tag that selector reads (NSTextFinder actions)
// -----------------------------------------------------------------------------
namespace {

    struct CommandRow
    {
        const char* id;
        const char* title;
        const char* category;
        const char* symbol;
        const char* mac;
        const char* vs;
        const char* ride;
        const char* responder;
        int tag;
    };

    const CommandRow kRows[] = {
        // ------------------------------------------------------------- File
        { "File.NewProject",    "Project...",                 "File", "new",        "Cmd+Shift+N", nullptr, nullptr, nullptr, 0 },
        { "File.NewWorkspace",  "Workspace...",               "File", "workspace",  "",            nullptr, nullptr, nullptr, 0 },
        { "File.NewFile",       "File...",                    "File", "file",       "Cmd+N",       nullptr, nullptr, nullptr, 0 },
        { "File.OpenWorkspace", "Project / Workspace...",     "File", "open",       "Cmd+Shift+O", nullptr, nullptr, nullptr, 0 },
        { "File.OpenFile",      "File...",                    "File", "open",       "Cmd+O",       nullptr, nullptr, nullptr, 0 },
        { "File.OpenCcs",       "CCS 7.4 / 5.5 Project...",   "File", "projectccs", "",            nullptr, nullptr, nullptr, 0 },
        { "File.ImportCcs",     "CCS 7.4 / 5.5 Project...",   "File", "projectccs", "",            nullptr, nullptr, nullptr, 0 },
        { "File.ImportFolder",  "Existing Folder...",         "File", "folder",     "",            nullptr, nullptr, nullptr, 0 },
        { "File.Close",         "Close",                      "File", "close",      "Cmd+W",       nullptr, nullptr, nullptr, 0 },
        { "File.CloseWorkspace","Close Workspace",            "File", nullptr,      "Alt+Cmd+Shift+W", nullptr, nullptr, nullptr, 0 },
        { "File.Save",          "Save",                       "File", "save",       "Cmd+S",       nullptr, nullptr, nullptr, 0 },
        { "File.SaveAs",        "Save As...",                 "File", nullptr,      "Cmd+Shift+S", nullptr, nullptr, nullptr, 0 },
        { "File.SaveAll",       "Save All",                   "File", "saveall",    "Alt+Cmd+S",   nullptr, nullptr, nullptr, 0 },
        { "File.ExportZip",     "Export Project as ZIP...",   "File", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "File.PageSetup",     "Page Setup...",              "File", nullptr,      "Cmd+Shift+P", nullptr, nullptr, "runPageLayout:", 0 },
        { "File.Print",         "Print...",                   "File", nullptr,      "Cmd+P",       nullptr, nullptr, nullptr, 0 },
        { "File.Exit",          "Quit GRIDEm",                "File", nullptr,      "Cmd+Q",       nullptr, nullptr, "terminate:", 0 },
        { "File.OpenSelected",  "Open",                       "File", "open",       "",            nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Edit
        { "Edit.Undo",          "Undo",                       "Edit", "undo",       "Cmd+Z",       nullptr, nullptr, "undo:", 0 },
        { "Edit.Redo",          "Redo",                       "Edit", "redo",       "Cmd+Shift+Z", nullptr, nullptr, "redo:", 0 },
        { "Edit.Cut",           "Cut",                        "Edit", "cut",        "Cmd+X",       nullptr, nullptr, "cut:", 0 },
        { "Edit.Copy",          "Copy",                       "Edit", "copy",       "Cmd+C",       nullptr, nullptr, "copy:", 0 },
        { "Edit.Paste",         "Paste",                      "Edit", "paste",      "Cmd+V",       nullptr, nullptr, "paste:", 0 },
        { "Edit.Delete",        "Delete",                     "Edit", nullptr,      "",            nullptr, nullptr, "delete:", 0 },
        { "Edit.SelectAll",     "Select All",                 "Edit", nullptr,      "Cmd+A",       nullptr, nullptr, "selectAll:", 0 },
        // Find, in the Find menu; NSTextFinderAction values: ShowFindInterface 1, Next 2, Previous 3,
        // SetSearchString 7, ShowReplaceInterface 12
        { "Edit.Find",          "Find...",                    "Find", "find",       "Cmd+F",       nullptr, nullptr, "performTextFinderAction:", 1 },
        { "Edit.FindNext",      "Find Next",                  "Find", nullptr,      "Cmd+G",       nullptr, nullptr, "performTextFinderAction:", 2 },
        { "Edit.FindPrevious",  "Find Previous",              "Find", nullptr,      "Cmd+Shift+G", nullptr, nullptr, "performTextFinderAction:", 3 },
        { "Edit.UseSelectionForFind", "Use Selection for Find", "Find", nullptr,   "Cmd+E",       nullptr, nullptr, "performTextFinderAction:", 7 },
        { "Edit.Replace",       "Find and Replace...",        "Find", nullptr,      "Alt+Cmd+F",   nullptr, nullptr, "performTextFinderAction:", 12 },
        { "Edit.FindInFiles",   "Find in Files...",           "Find", "findfiles",  "Cmd+Shift+F", nullptr, nullptr, nullptr, 0 },
        { "Edit.GoToLine",      "Go to Line...",              "Find", nullptr,      "Cmd+L",       nullptr, nullptr, nullptr, 0 },
        { "Edit.JumpToDefinition", "Jump to Definition",      "Find", nullptr,      "Ctrl+Cmd+J",  nullptr, nullptr, nullptr, 0 },
        { "Edit.Comment",       "Comment Selection",          "Edit", "comment",    "Cmd+/",       "Ctrl+K,C", nullptr, nullptr, 0 },
        { "Edit.Uncomment",     "Uncomment Selection",        "Edit", "uncomment",  "Alt+Cmd+/",   "Ctrl+K,U", nullptr, nullptr, 0 },
        { "Edit.Format",        "Format Document",            "Edit", nullptr,      "Ctrl+I",      "Ctrl+K,D", nullptr, nullptr, 0 },
        { "Edit.Upper",         "Make Uppercase",             "Edit", nullptr,      "Ctrl+Shift+U", nullptr, nullptr, nullptr, 0 },
        { "Edit.Lower",         "Make Lowercase",             "Edit", nullptr,      "Ctrl+U",      nullptr, nullptr, nullptr, 0 },
        { "Edit.Tabify",        "Tabify / Untabify",          "Edit", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Edit.Indent",        "Shift Right",                "Edit", "indent",     "Cmd+]",       nullptr, nullptr, nullptr, 0 },
        { "Edit.Outdent",       "Shift Left",                 "Edit", "outdent",    "Cmd+[",       nullptr, nullptr, nullptr, 0 },
        { "Edit.ToggleBookmark","Toggle Bookmark",            "Edit", "bookmark",   "Cmd+F2",      "Ctrl+F2", nullptr, nullptr, 0 },
        { "Edit.NextBookmark",  "Next Bookmark",              "Edit", "nextbm",     "F2",          nullptr, nullptr, nullptr, 0 },
        { "Edit.PrevBookmark",  "Previous Bookmark",          "Edit", "prevbm",     "Shift+F2",    nullptr, nullptr, nullptr, 0 },
        { "Edit.ClearBookmarks","Clear All Bookmarks",        "Edit", nullptr,      "",            nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- View
        { "View.Navigator",     "Navigator",                  "View", "navigator",  "Cmd+0",       nullptr, nullptr, nullptr, 0 },
        { "View.Inspector",     "Inspector",                  "View", "inspector",  "Alt+Cmd+0",   nullptr, nullptr, nullptr, 0 },
        { "View.BottomArea",    "Bottom Area",                "View", "bottomarea", "Cmd+Shift+Y", nullptr, nullptr, nullptr, 0 },
        { "View.ProjectExplorer","Workspace",                 "View", "explorer",   "Cmd+1",       "Ctrl+Alt+L", nullptr, nullptr, 0 },
        { "View.ClassView",     "Class View",                 "View", "classview",  "Cmd+2",       "Ctrl+Shift+C", nullptr, nullptr, 0 },
        { "View.Files",         "Files",                      "View", "files",      "Cmd+3",       nullptr, nullptr, nullptr, 0 },
        { "View.FindResults",   "Find Results",               "View", "findresults","Cmd+4",       nullptr, nullptr, nullptr, 0 },
        { "View.ErrorList",     "Issues",                     "View", "errorlist",  "Cmd+5",       "Ctrl+\\,E", nullptr, nullptr, 0 },
        { "View.Properties",    "Properties",                 "View", "properties", "Alt+Cmd+1",   "F4", "", nullptr, 0 },
        { "View.Output",        "Output",                     "View", "output",     "Cmd+Shift+C", "Ctrl+Alt+O", nullptr, nullptr, 0 },
        { "View.Terminal",      "Terminal",                   "View", "terminal",   "Ctrl+`",      nullptr, nullptr, nullptr, 0 },
        { "View.StatusBar",     "Status Bar",                 "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.Toolbar",       "Toolbar",                    "View", nullptr,      "Alt+Cmd+T",   nullptr, nullptr, "toggleToolbarShown:", 0 },
        { "View.FullScreen",    "Enter Full Screen",          "View", nullptr,      "Ctrl+Cmd+F",  nullptr, nullptr, "toggleFullScreen:", 0 },
        { "View.ThemeSystem",   "System",                     "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.ThemeDark",     "Dark",                       "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.ThemeLight",    "Light",                      "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.ThemeBlue",     "Blue",                       "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.ZoomIn",        "Zoom In",                    "View", nullptr,      "Cmd+=",       nullptr, nullptr, nullptr, 0 },
        { "View.ZoomOut",       "Zoom Out",                   "View", nullptr,      "Cmd+-",       nullptr, nullptr, nullptr, 0 },
        { "View.ZoomReset",     "Actual Size",                "View", nullptr,      "Ctrl+Cmd+0",  nullptr, nullptr, nullptr, 0 },
        { "View.LineNumbers",   "Line Numbers",               "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "View.Whitespace",    "Show Invisibles",            "View", nullptr,      "",            nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Project
        { "Project.AddNewItem", "Add New Item...",            "Project", "new",     "Alt+Cmd+N",   "Ctrl+Shift+A", nullptr, nullptr, 0 },
        { "Project.AddExisting","Add Existing Item...",       "Project", "open",    "Alt+Cmd+A",   "Shift+Alt+A", nullptr, nullptr, 0 },
        { "Project.NewFolder",  "New Folder...",              "Project", "folder",  "",            nullptr, nullptr, nullptr, 0 },
        { "Project.Remove",     "Remove from Project",        "Project", "close",   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.Rename",     "Rename",                     "Project", nullptr,   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.Exclude",    "Exclude From Build",         "Project", nullptr,   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.AddProject", "Add Project to Workspace...","Project", "project", "",            nullptr, nullptr, nullptr, 0 },
        { "Project.RemoveProject","Remove Project from Workspace","Project", nullptr,"",           nullptr, nullptr, nullptr, 0 },
        { "Project.SetStartup", "Set as Startup Project",     "Project", nullptr,   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.Dependencies","Project Dependencies...",   "Project", nullptr,   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.BuildOrder", "Build Order...",             "Project", nullptr,   "",            nullptr, nullptr, nullptr, 0 },
        { "Project.RefreshCcs", "Refresh Imported Project",   "Project", "refresh", "",            nullptr, nullptr, nullptr, 0 },
        { "Project.CcsMapping", "CCS / VS Mapping...",        "Project", "info",    "",            nullptr, nullptr, nullptr, 0 },
        { "Project.OpenFolder", "Show in Finder",             "Project", "folderopen", "",         nullptr, nullptr, nullptr, 0 },
        { "Project.CompilerOptions","Compiler Options...",    "Project", "settings","",            nullptr, nullptr, nullptr, 0 },
        { "Project.Properties", "Project Settings...",        "Project", "properties", "Alt+Cmd+P", "Alt+F7", nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Build
        { "Build.BuildWorkspace","Build Workspace",           "Build", "build",     "Cmd+Shift+B", "F7", "F4", nullptr, 0 },
        { "Build.RebuildWorkspace","Rebuild Workspace",       "Build", "rebuild",   "",            nullptr, nullptr, nullptr, 0 },
        { "Build.CleanWorkspace","Clean Workspace",           "Build", "clean",     "Alt+Cmd+Shift+K", nullptr, nullptr, nullptr, 0 },
        { "Build.BuildProject", "Build Project",              "Build", "build",     "Cmd+B",       "Ctrl+B", "", nullptr, 0 },
        { "Build.RebuildProject","Rebuild Project",           "Build", "rebuild",   "",            nullptr, nullptr, nullptr, 0 },
        { "Build.CleanProject", "Clean Project",              "Build", "clean",     "Cmd+Shift+K", nullptr, nullptr, nullptr, 0 },
        { "Build.Compile",      "Compile",                    "Build", nullptr,     "Ctrl+Cmd+B",  "Ctrl+F7", "Ctrl+B", nullptr, 0 },
        { "Build.AssembleOnly", "Assemble Only (ASM6x / MASM)","Build", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Build.LinkOnly",     "Link Only (LNK6x / LINK)",   "Build", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Build.Batch",        "Batch Build...",             "Build", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Build.ConfigManager","Configuration Manager...",   "Build", "settings",  "",            nullptr, nullptr, nullptr, 0 },
        { "Build.Cancel",       "Cancel Build",               "Build", "stop",      "Alt+Cmd+.",   nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Debug
        { "Debug.Start",        "Start Debugging",            "Debug", "debug",     "Cmd+R",       "F5", "F8", nullptr, 0 },
        { "Debug.StartNoDebug", "Start Without Debugging",    "Debug", "run",       "Ctrl+Cmd+R",  "Ctrl+F5", "F5", nullptr, 0 },
        { "Debug.Emulate",      "Emulate on VM6747",          "Debug", "emulate",   "",            nullptr, nullptr, nullptr, 0 },
        { "Debug.Verify",       "Verify (vm6747 against sim6747)", "Debug", nullptr,"",            nullptr, nullptr, nullptr, 0 },
        { "Debug.StopProgram",  "Stop Program",               "Debug", "stop",      "",            nullptr, nullptr, nullptr, 0 },
        { "Debug.Stop",         "Stop Debugging",             "Debug", "stop",      "Cmd+.",       "Shift+F5", nullptr, nullptr, 0 },
        { "Debug.Restart",      "Restart",                    "Debug", "restart",   "Ctrl+Cmd+Shift+R", "Ctrl+Shift+F5", nullptr, nullptr, 0 },
        { "Debug.BreakAll",     "Pause",                      "Debug", "pause",     "Ctrl+Cmd+Y",  nullptr, nullptr, nullptr, 0 },
        { "Debug.StepInto",     "Step Into",                  "Debug", "stepinto",  "F7",          "F11", "F6", nullptr, 0 },
        { "Debug.StepOver",     "Step Over",                  "Debug", "stepover",  "F6",          "F10", "F7", nullptr, 0 },
        { "Debug.StepOut",      "Step Out",                   "Debug", "stepout",   "F8",          "Shift+F11", nullptr, nullptr, 0 },
        { "Debug.RunToCursor",  "Run to Cursor",              "Debug", nullptr,     "Ctrl+Cmd+C",  "Ctrl+F10", nullptr, nullptr, 0 },
        { "Debug.UpStack",      "Up the Stack",               "Debug", nullptr,     "Ctrl+Cmd+Up", nullptr, "Ctrl+Up", nullptr, 0 },
        { "Debug.DownStack",    "Down the Stack",             "Debug", nullptr,     "Ctrl+Cmd+Down", nullptr, "Ctrl+Down", nullptr, 0 },
        { "Debug.AddWatch",     "Watch Expression...",        "Debug", "watch",     "",            nullptr, nullptr, nullptr, 0 },
        { "Debug.SetVariable",  "Set Variable...",            "Debug", "variable",  "",            nullptr, nullptr, nullptr, 0 },
        { "Debug.ToggleBreakpoint","Toggle Breakpoint",       "Debug", "breakpoint","Cmd+\\",      "F9", "F9", nullptr, 0 },
        { "Debug.DeleteAllBreakpoints","Delete All Breakpoints","Debug", nullptr,   "Cmd+Shift+\\","Ctrl+Shift+F9", nullptr, nullptr, 0 },
        { "Debug.WinWatch",     "Watch",                      "Debug", "watch",     "",            "Ctrl+Alt+W", nullptr, nullptr, 0 },
        { "Debug.WinLocals",    "Locals",                     "Debug", "locals",    "",            "Ctrl+Alt+V", nullptr, nullptr, 0 },
        { "Debug.WinCallStack", "Call Stack",                 "Debug", "callstack", "",            "Ctrl+Alt+C", nullptr, nullptr, 0 },
        { "Debug.WinRegisters", "Registers",                  "Debug", "registers", "",            "Ctrl+Alt+G", nullptr, nullptr, 0 },
        { "Debug.WinMemory",    "Memory",                     "Debug", "memory",    "",            "Ctrl+Alt+M", nullptr, nullptr, 0 },
        { "Debug.WinDisassembly","Disassembly",               "Debug", "disassembly","",           "Ctrl+Alt+D", nullptr, nullptr, 0 },
        { "Debug.WinBreakpoints","Breakpoints",               "Debug", "breakpoint","Cmd+8",       "Ctrl+Alt+B", nullptr, nullptr, 0 },
        { "Debug.Connect",      "Connect to VM6747...",       "Debug", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Debug.LoadProgram",  "Load Program (.out)...",     "Debug", "open",      "",            nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Tools
        { "Tools.CToShalimar",  "C  →  Shalimar...",     "Tools", "convert",   "Ctrl+Alt+Cmd+S", "Ctrl+Alt+S", nullptr, nullptr, 0 },
        { "Tools.ShalimarToC",  "Shalimar  →  C...",     "Tools", "convert",   "Ctrl+Alt+Cmd+H", "Ctrl+Alt+H", nullptr, nullptr, 0 },
        { "Tools.ToolchainCheck","Toolchain Check...",        "Tools", "info",      "",            nullptr, nullptr, nullptr, 0 },
        { "Tools.CommandPrompt","Open in Terminal",           "Tools", "terminal",  "",            nullptr, nullptr, nullptr, 0 },
        { "Tools.ExternalTools","External Tools...",          "Tools", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Tools.Snippets",     "Code Snippets...",           "Tools", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Tools.Keyboard",     "Customize Keyboard...",      "Tools", nullptr,     "",            nullptr, nullptr, nullptr, 0 },
        { "Tools.Options",      "Settings...",                "Tools", "settings",  "Cmd+,",       nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Window
        { "Window.Split",       "Split Editor",               "Window", "split",    "Ctrl+Cmd+T",  nullptr, nullptr, nullptr, 0 },
        { "Window.Float",       "Open Pane in Separate Window","Window", "float",   "",            nullptr, nullptr, nullptr, 0 },
        { "Window.Dock",        "Dock Pane",                  "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Window.AutoHide",    "Hide All Panes",             "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Window.PinTab",      "Pin Tab",                    "Window", "pin",      "",            nullptr, nullptr, nullptr, 0 },
        { "Window.CloseAll",    "Close All Documents",        "Window", nullptr,    "Alt+Cmd+W",   nullptr, nullptr, nullptr, 0 },
        { "Window.CloseAllButThis","Close Other Documents",   "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Window.ResetLayout", "Reset Window Layout",        "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Window.SaveLayout",  "Save Window Layout",         "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },
        { "Window.Windows",     "Documents...",               "Window", nullptr,    "",            nullptr, nullptr, nullptr, 0 },

        // ------------------------------------------------------------- Help
        { "Help.ViewHelp",      "GRIDEm Help",                "Help", "help",       "Cmd+?",       "F1", nullptr, nullptr, 0 },
        { "Help.UserGuide",     "GRIDE User Guide",           "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefC90",        "C90",                        "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefCpp11",      "C++11",                      "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefShalimar",   "Shalimar",                   "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefMasm",       "MASM / LINK (Windows)",      "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefAsm6x",      "ASM6x / LNK6x",              "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefRts",        "RTS6747 Runtime",            "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.RefVm",         "VM6747 / SIM6747",           "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.Shortcuts",     "Keyboard Shortcuts",         "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.ReleaseNotes",  "Release Notes",              "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
        { "Help.About",         "About GRIDEm",               "Help", nullptr,      "",            nullptr, nullptr, nullptr, 0 },
    };

    NSString* Str(const char* s) { return s == nullptr ? nil : [NSString stringWithUTF8String:s]; }

    const CommandRow* RowFor(NSString* commandId)
    {
        for (const CommandRow& r : kRows)
            if ([commandId isEqualToString:Str(r.id)]) return &r;
        return nullptr;
    }

    // the key a row has in a key set: ride falls back to vs, vs to mac
    NSString* KeyOf(const CommandRow& r, NSString* keySet)
    {
        const char* k = r.mac;
        if ([keySet isEqualToString:@"visualstudio"] || [keySet isEqualToString:@"ride"])
            if (r.vs != nullptr) k = r.vs;
        if ([keySet isEqualToString:@"ride"] && r.ride != nullptr) k = r.ride;
        return Str(k);
    }

    // one item of a menu layout: a command id, "-" a separator, ">Title" opens a submenu, "<" closes it
    const char* const kMenuLayout[] = {
        "#File",
        ">New", "File.NewProject", "File.NewWorkspace", "File.NewFile", "<",
        ">Open", "File.OpenWorkspace", "File.OpenFile", "File.OpenCcs", "<",
        "@recentFiles", "@recentWorkspaces",
        "-", "File.Close", "File.CloseWorkspace", "-", "File.Save", "File.SaveAs", "File.SaveAll", "-",
        ">Import", "File.ImportCcs", "File.ImportFolder", "<",
        "File.ExportZip", "-", "File.PageSetup", "File.Print",
        "#Edit",
        "Edit.Undo", "Edit.Redo", "-", "Edit.Cut", "Edit.Copy", "Edit.Paste", "Edit.Delete", "Edit.SelectAll", "-",
        ">Advanced", "Edit.Comment", "Edit.Uncomment", "Edit.Format", "-", "Edit.Upper", "Edit.Lower", "Edit.Tabify", "-",
        "Edit.Indent", "Edit.Outdent", "<",
        ">Bookmarks", "Edit.ToggleBookmark", "Edit.NextBookmark", "Edit.PrevBookmark", "Edit.ClearBookmarks", "<",
        "#Find",
        "Edit.Find", "Edit.Replace", "Edit.FindNext", "Edit.FindPrevious", "Edit.UseSelectionForFind", "-",
        "Edit.FindInFiles", "-", "Edit.GoToLine", "Edit.JumpToDefinition",
        "#View",
        "View.Navigator", "View.Inspector", "View.BottomArea", "-",
        ">Navigators", "View.ProjectExplorer", "View.ClassView", "View.Files", "View.FindResults", "Debug.WinBreakpoints", "<",
        "View.Properties", "View.Output", "View.ErrorList", "View.Terminal", "-",
        "View.Toolbar", "@toolbars", "View.StatusBar", "View.FullScreen", "-",
        ">Theme", "View.ThemeSystem", "-", "View.ThemeDark", "View.ThemeLight", "View.ThemeBlue", "<",
        "-", "View.ZoomIn", "View.ZoomOut", "View.ZoomReset", "-", "View.LineNumbers", "View.Whitespace",
        "#Project",
        "Project.AddNewItem", "Project.AddExisting", "Project.NewFolder", "Project.Remove", "Project.Rename", "Project.Exclude", "-",
        "Project.AddProject", "Project.RemoveProject", "Project.SetStartup", "-", "Project.Dependencies", "Project.BuildOrder", "-",
        "Project.RefreshCcs", "Project.CcsMapping", "Project.OpenFolder", "-", "Project.CompilerOptions", "Project.Properties",
        "#Build",
        "Build.BuildWorkspace", "Build.RebuildWorkspace", "Build.CleanWorkspace", "-",
        "Build.BuildProject", "Build.RebuildProject", "Build.CleanProject", "-",
        "Build.Compile", "Build.AssembleOnly", "Build.LinkOnly", "-", "Build.Batch", "Build.ConfigManager", "-", "Build.Cancel",
        "#Debug",
        "Debug.Start", "Debug.StartNoDebug", "Debug.Emulate", "Debug.Verify", "Debug.StopProgram", "-",
        "Debug.Stop", "Debug.Restart", "Debug.BreakAll", "-",
        "Debug.StepInto", "Debug.StepOver", "Debug.StepOut", "Debug.RunToCursor", "-",
        "Debug.UpStack", "Debug.DownStack", "Debug.AddWatch", "Debug.SetVariable", "-",
        "Debug.ToggleBreakpoint", "Debug.DeleteAllBreakpoints", "-",
        ">Debug Panes", "Debug.WinWatch", "Debug.WinLocals", "Debug.WinCallStack", "Debug.WinRegisters", "Debug.WinMemory",
        "Debug.WinDisassembly", "Debug.WinBreakpoints", "<",
        "-", "Debug.Connect", "Debug.LoadProgram",
        "#Tools",
        ">Convert", "Tools.CToShalimar", "Tools.ShalimarToC", "<",
        "-", "Tools.ToolchainCheck", "Tools.CommandPrompt", "Tools.ExternalTools", "@externalTools", "-",
        "Tools.Snippets", "Tools.Keyboard",
        "#Window",
        "@minimize", "@zoom", "-",
        "Window.Split", "Window.Float", "Window.Dock", "Window.AutoHide", "Window.PinTab", "-",
        "Window.CloseAll", "Window.CloseAllButThis", "-", "Window.ResetLayout", "Window.SaveLayout", "-", "Window.Windows", "-",
        "@front",
        "#Help",
        "Help.ViewHelp", "Help.UserGuide",
        ">Language Reference", "Help.RefC90", "Help.RefCpp11", "Help.RefShalimar", "<",
        ">Toolchain Reference", "Help.RefMasm", "Help.RefAsm6x", "Help.RefRts", "Help.RefVm", "<",
        "Help.Shortcuts", "-", "Help.ReleaseNotes",
    };
}

// =============================================================================
@implementation GRCommand

- (instancetype)initWithId:(NSString*)commandId title:(NSString*)title category:(NSString*)category
                    symbol:(NSString*)symbol
{
    self = [super init];
    if (self)
    {
        _commandId = [commandId copy];
        _title = [title copy];
        _category = [category copy];
        _symbolName = [symbol copy];
        _shortcutText = @"";
        _responderAction = nullptr;
        _responderTag = 0;
    }
    return self;
}

- (BOOL)enabled
{
    if (self.handler == nil) return NO;
    if (self.canExecute == nil) return YES;
    return self.canExecute();
}

- (BOOL)checked
{
    if (self.isChecked == nil) return NO;
    return self.isChecked();
}

- (void)execute
{
    if (![self enabled]) return;
    @try { self.handler(); }
    @catch (NSException* ex)
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithFormat:@"GRIDEm - %@", self.title];
        alert.informativeText = GROr(ex.reason, ex.name);
        alert.alertStyle = NSAlertStyleCritical;
        [alert runModal];
    }
}

- (BOOL)isChord
{
    NSString* s = self.shortcutText;
    return s.length > 1 && [s rangeOfString:@","].location != NSNotFound && ![s hasSuffix:@","];
}
@end

// =============================================================================
@interface GRCommandRegistry ()
{
    NSMutableArray<GRCommand*>* ordered;
    NSMutableDictionary<NSString*, GRCommand*>* byId;     // lower-case id -> command
    NSString* currentKeySet;
    id chordMonitor;
    NSString* pendingChord;                                // "Ctrl+K" while waiting for the second key
}
@end

@implementation GRCommandRegistry

+ (GRCommandRegistry*)shared
{
    static GRCommandRegistry* instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [[GRCommandRegistry alloc] init]; });
    return instance;
}

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        ordered = [NSMutableArray array];
        byId = [NSMutableDictionary dictionary];
        currentKeySet = @"macos";
        for (const CommandRow& r : kRows)
        {
            GRCommand* c = [[GRCommand alloc] initWithId:Str(r.id) title:Str(r.title) category:Str(r.category)
                                                  symbol:Str(r.symbol)];
            c.shortcutText = Str(r.mac);
            if (r.responder != nullptr)
            {
                c.responderAction = NSSelectorFromString(Str(r.responder));
                c.responderTag = r.tag;
            }
            [ordered addObject:c];
            byId[c.commandId.lowercaseString] = c;
        }
    }
    return self;
}

- (NSArray<GRCommand*>*)all { return ordered; }
- (NSString*)keySet { return currentKeySet; }

- (GRCommand*)command:(NSString*)commandId
{
    if (commandId == nil) return nil;
    return byId[commandId.lowercaseString];
}

- (GRCommand*)addCommand:(NSString*)commandId title:(NSString*)title category:(NSString*)category
                  symbol:(NSString*)symbol shortcut:(NSString*)shortcut
{
    GRCommand* c = [self command:commandId];
    if (c == nil)
    {
        c = [[GRCommand alloc] initWithId:commandId title:title category:category symbol:symbol];
        [ordered addObject:c];
        byId[commandId.lowercaseString] = c;
    }
    c.title = title;
    c.shortcutText = GROr(shortcut, @"");
    return c;
}

- (GRCommand*)bind:(NSString*)commandId handler:(GRCommandHandler)handler
{
    return [self bind:commandId handler:handler when:nil];
}

- (GRCommand*)bind:(NSString*)commandId handler:(GRCommandHandler)handler when:(GRCommandTest)canExecute
{
    GRCommand* c = [self command:commandId];
    if (c == nil) c = [self addCommand:commandId title:commandId category:@"" symbol:nil shortcut:@""];
    c.handler = handler;
    c.canExecute = canExecute;
    return c;
}

- (void)setChecked:(NSString*)commandId test:(GRCommandTest)isChecked
{
    [self command:commandId].isChecked = isChecked;
}

- (void)execute:(NSString*)commandId
{
    GRCommand* c = [self command:commandId];
    if (c == nil) return;
    if (c.responderAction != nullptr && c.handler == nil)
    {
        NSMenuItem* sender = [[NSMenuItem alloc] init];
        sender.tag = c.responderTag;
        [NSApp sendAction:c.responderAction to:nil from:sender];
        return;
    }
    [c execute];
}

// ---- key sets ----------------------------------------------------------------
+ (NSArray<NSString*>*)keySetNames { return @[ @"macos", @"visualstudio", @"ride" ]; }

+ (NSString*)describeKeySet:(NSString*)keySet
{
    if ([keySet isEqualToString:@"ride"])
        return @"RIDE (F8 debugs or carries on, F7 / F6 step over / into, F9 breakpoint, Ctrl+Up / Ctrl+Down the stack, "
               @"F4 builds, F5 runs, Ctrl+B compiles)";
    if ([keySet isEqualToString:@"visualstudio"])
        return @"Visual Studio (F5 debugs, F10 / F11 step over / into, Shift+F11 steps out, F9 breakpoint, F7 builds, "
               @"Ctrl+F5 runs)";
    return @"macOS (⌘R debugs, ⌃⌘R runs, F6 / F7 / F8 step over / into / out, ⌘\\ breakpoint, "
           @"⌘B builds the project, ⇧⌘B the workspace)";
}

- (NSString*)defaultShortcut:(NSString*)commandId inKeySet:(NSString*)keySet
{
    const CommandRow* r = RowFor(GROr([self command:commandId].commandId, commandId));
    if (r == nullptr) return @"";
    return GROr(KeyOf(*r, keySet), @"");
}

// a key taken by one command is given back as nothing by any other that had it (GRIDEd's KeySets)
- (void)takeKey:(NSString*)key forCommand:(GRCommand*)owner
{
    if (key.length == 0) return;
    for (GRCommand* c in ordered)
        if (c != owner && [c.shortcutText caseInsensitiveCompare:key] == NSOrderedSame) c.shortcutText = @"";
}

- (void)applyKeySet:(NSString*)keySet bindings:(NSDictionary<NSString*, NSString*>*)bindings
{
    NSString* set = keySet.length > 0 ? keySet.lowercaseString : @"macos";
    if (![[GRCommandRegistry keySetNames] containsObject:set]) set = @"macos";
    currentKeySet = set;
    for (GRCommand* c in ordered)
    {
        const CommandRow* r = RowFor(c.commandId);
        if (r != nullptr) c.shortcutText = GROr(KeyOf(*r, set), @"");
    }
    // RIDE's keys over Visual Studio's take theirs from the others
    if ([set isEqualToString:@"ride"])
        for (GRCommand* c in ordered)
        {
            const CommandRow* r = RowFor(c.commandId);
            if (r != nullptr && r->ride != nullptr) [self takeKey:c.shortcutText forCommand:c];
        }
    for (NSString* commandId in bindings)
    {
        GRCommand* c = [self command:commandId];
        if (c == nil) continue;
        c.shortcutText = GROr(bindings[commandId], @"");
        [self takeKey:c.shortcutText forCommand:c];
    }
}

// ---- key text ----------------------------------------------------------------
+ (BOOL)parseKey:(NSString*)text equivalent:(NSString**)equivalent modifiers:(NSEventModifierFlags*)modifiers
{
    NSString* s = [text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (s.length == 0) return NO;
    if (s.length > 1 && [s rangeOfString:@","].location != NSNotFound && ![s hasSuffix:@","]) return NO;
    NSEventModifierFlags mods = 0;
    NSString* key = nil;
    // a trailing "+" key, e.g. "Cmd++"
    if ([s hasSuffix:@"++"]) { key = @"+"; s = [s substringToIndex:s.length - 2]; }
    NSArray<NSString*>* parts = [s componentsSeparatedByString:@"+"];
    for (NSString* raw in parts)
    {
        NSString* p = [raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (p.length == 0) continue;
        NSString* u = p.uppercaseString;
        if ([u isEqualToString:@"CTRL"] || [u isEqualToString:@"CONTROL"]) mods |= NSEventModifierFlagControl;
        else if ([u isEqualToString:@"SHIFT"]) mods |= NSEventModifierFlagShift;
        else if ([u isEqualToString:@"ALT"] || [u isEqualToString:@"OPTION"] || [u isEqualToString:@"OPT"])
            mods |= NSEventModifierFlagOption;
        else if ([u isEqualToString:@"CMD"] || [u isEqualToString:@"COMMAND"]) mods |= NSEventModifierFlagCommand;
        else key = p;
    }
    if (key.length == 0) return NO;
    NSString* u = key.uppercaseString;
    unichar fk = 0;
    if ([u isEqualToString:@"DEL"] || [u isEqualToString:@"DELETE"]) fk = NSDeleteFunctionKey;
    else if ([u isEqualToString:@"BACKSPACE"]) fk = NSBackspaceCharacter;
    else if ([u isEqualToString:@"INS"] || [u isEqualToString:@"INSERT"]) fk = NSHelpFunctionKey;
    else if ([u isEqualToString:@"ENTER"] || [u isEqualToString:@"RETURN"]) fk = NSCarriageReturnCharacter;
    else if ([u isEqualToString:@"ESC"] || [u isEqualToString:@"ESCAPE"]) fk = 0x1B;
    else if ([u isEqualToString:@"TAB"]) fk = NSTabCharacter;
    else if ([u isEqualToString:@"SPACE"]) fk = ' ';
    else if ([u isEqualToString:@"HOME"]) fk = NSHomeFunctionKey;
    else if ([u isEqualToString:@"END"]) fk = NSEndFunctionKey;
    else if ([u isEqualToString:@"PGUP"]) fk = NSPageUpFunctionKey;
    else if ([u isEqualToString:@"PGDN"]) fk = NSPageDownFunctionKey;
    else if ([u isEqualToString:@"UP"]) fk = NSUpArrowFunctionKey;
    else if ([u isEqualToString:@"DOWN"]) fk = NSDownArrowFunctionKey;
    else if ([u isEqualToString:@"LEFT"]) fk = NSLeftArrowFunctionKey;
    else if ([u isEqualToString:@"RIGHT"]) fk = NSRightArrowFunctionKey;
    else if ([u isEqualToString:@"BREAK"] || [u isEqualToString:@"PAUSE"]) fk = NSPauseFunctionKey;
    else if (u.length >= 2 && u.length <= 3 && [u characterAtIndex:0] == 'F')
    {
        int n = [[u substringFromIndex:1] intValue];
        if (n >= 1 && n <= 35) fk = (unichar)(NSF1FunctionKey + n - 1);
    }
    NSString* eq = nil;
    if (fk != 0) eq = [NSString stringWithCharacters:&fk length:1];
    else if (key.length == 1)
    {
        // a shifted character carries its own shift: "Cmd+?" is Shift+Cmd+/
        eq = key.lowercaseString;
    }
    else return NO;
    if (equivalent != nullptr) *equivalent = eq;
    if (modifiers != nullptr) *modifiers = mods;
    return YES;
}

+ (NSString*)displayText:(NSString*)text
{
    if (text.length == 0) return @"";
    NSArray<NSString*>* chord = [text componentsSeparatedByString:@","];
    if (chord.count == 2 && [chord[1] length] > 0)
        return [NSString stringWithFormat:@"%@, %@", [self displayText:chord[0]], [self displayText:chord[1]]];
    NSString* eq = nil;
    NSEventModifierFlags mods = 0;
    if (![self parseKey:text equivalent:&eq modifiers:&mods]) return text;
    NSMutableString* r = [NSMutableString string];
    if (mods & NSEventModifierFlagControl) [r appendString:@"⌃"];
    if (mods & NSEventModifierFlagOption) [r appendString:@"⌥"];
    if (mods & NSEventModifierFlagShift) [r appendString:@"⇧"];
    if (mods & NSEventModifierFlagCommand) [r appendString:@"⌘"];
    unichar c = [eq characterAtIndex:0];
    if (c >= NSF1FunctionKey && c <= NSF35FunctionKey) [r appendFormat:@"F%d", (int)(c - NSF1FunctionKey + 1)];
    else if (c == NSUpArrowFunctionKey) [r appendString:@"↑"];
    else if (c == NSDownArrowFunctionKey) [r appendString:@"↓"];
    else if (c == NSLeftArrowFunctionKey) [r appendString:@"←"];
    else if (c == NSRightArrowFunctionKey) [r appendString:@"→"];
    else if (c == NSDeleteFunctionKey) [r appendString:@"⌦"];
    else if (c == NSBackspaceCharacter) [r appendString:@"⌫"];
    else if (c == NSCarriageReturnCharacter) [r appendString:@"↩"];
    else if (c == 0x1B) [r appendString:@"⎋"];
    else if (c == NSTabCharacter) [r appendString:@"⇥"];
    else if (c == ' ') [r appendString:@"Space"];
    else [r appendString:eq.uppercaseString];
    return r;
}

+ (NSString*)keyTextOfEvent:(NSEvent*)event
{
    NSMutableString* r = [NSMutableString string];
    NSEventModifierFlags m = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if (m & NSEventModifierFlagControl) [r appendString:@"Ctrl+"];
    if (m & NSEventModifierFlagOption) [r appendString:@"Alt+"];
    if (m & NSEventModifierFlagShift) [r appendString:@"Shift+"];
    if (m & NSEventModifierFlagCommand) [r appendString:@"Cmd+"];
    NSString* chars = event.charactersIgnoringModifiers;
    if (chars.length == 0) return r;
    unichar c = [chars characterAtIndex:0];
    if (c >= NSF1FunctionKey && c <= NSF35FunctionKey) [r appendFormat:@"F%d", (int)(c - NSF1FunctionKey + 1)];
    else if (c == NSUpArrowFunctionKey) [r appendString:@"Up"];
    else if (c == NSDownArrowFunctionKey) [r appendString:@"Down"];
    else if (c == NSLeftArrowFunctionKey) [r appendString:@"Left"];
    else if (c == NSRightArrowFunctionKey) [r appendString:@"Right"];
    else if (c == NSDeleteFunctionKey) [r appendString:@"Del"];
    else if (c == NSDeleteCharacter || c == NSBackspaceCharacter) [r appendString:@"Backspace"];
    else if (c == NSCarriageReturnCharacter || c == NSEnterCharacter) [r appendString:@"Enter"];
    else if (c == 0x1B) [r appendString:@"Esc"];
    else if (c == NSTabCharacter) [r appendString:@"Tab"];
    else if (c == ' ') [r appendString:@"Space"];
    else [r appendString:[chars substringToIndex:1].uppercaseString];
    return r;
}

// ---- menus -----------------------------------------------------------------
- (void)applyKeyOf:(GRCommand*)c toItem:(NSMenuItem*)item
{
    NSString* eq = nil;
    NSEventModifierFlags mods = 0;
    if (![c isChord] && [GRCommandRegistry parseKey:c.shortcutText equivalent:&eq modifiers:&mods])
    {
        item.keyEquivalent = eq;
        item.keyEquivalentModifierMask = mods;
    }
    else
    {
        item.keyEquivalent = @"";
        item.keyEquivalentModifierMask = 0;
    }
    // a chord is shown in the tool tip, as a menu cannot show two keys
    item.toolTip = [c isChord] ? [GRCommandRegistry displayText:c.shortcutText] : nil;
}

- (NSMenuItem*)menuItem:(NSString*)commandId
{
    GRCommand* c = [self command:commandId];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:(c != nil ? c.title : commandId)
                                                  action:@selector(performCommand:) keyEquivalent:@""];
    item.representedObject = c.commandId;
    if (c == nil) return item;
    if (c.responderAction != nullptr)
    {
        item.action = c.responderAction;
        item.target = nil;
        item.tag = c.responderTag;
    }
    else item.target = self;
    [self applyKeyOf:c toItem:item];
    return item;
}

- (NSToolbarItem*)toolbarItem:(NSString*)commandId showText:(BOOL)showText
{
    GRCommand* c = [self command:commandId];
    NSToolbarItem* t = [[NSToolbarItem alloc] initWithItemIdentifier:commandId];
    t.label = GROr(c.title, commandId);
    t.paletteLabel = t.label;
    NSString* keys = [GRCommandRegistry displayText:c.shortcutText];
    t.toolTip = keys.length > 0 ? [NSString stringWithFormat:@"%@ (%@)", t.label, keys] : t.label;
    t.image = [GRIcons imageForCommand:commandId symbol:c.symbolName];
    if (c.responderAction != nullptr) { t.action = c.responderAction; t.target = nil; t.tag = c.responderTag; }
    else { t.action = @selector(performCommand:); t.target = self; }
    t.bordered = YES;
    if (!showText) t.label = @"";
    return t;
}

- (void)performCommand:(id)sender
{
    NSString* commandId = nil;
    if ([sender isKindOfClass:[NSMenuItem class]]) commandId = [(NSMenuItem*)sender representedObject];
    else if ([sender isKindOfClass:[NSToolbarItem class]]) commandId = [(NSToolbarItem*)sender itemIdentifier];
    [self execute:commandId];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    GRCommand* c = [self command:item.representedObject];
    if (c == nil) return YES;
    item.state = [c checked] ? NSControlStateValueOn : NSControlStateValueOff;
    return [c enabled];
}

- (BOOL)validateToolbarItem:(NSToolbarItem*)item
{
    GRCommand* c = [self command:item.itemIdentifier];
    if (c == nil) return YES;
    return [c enabled];
}

- (NSMenuItem*)topItem:(NSString*)title menu:(NSMenu**)menuOut
{
    NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    NSMenu* m = [[NSMenu alloc] initWithTitle:title];
    top.submenu = m;
    *menuOut = m;
    return top;
}

- (NSMenu*)appMenu
{
    NSMenu* m = [[NSMenu alloc] initWithTitle:@"GRIDEm"];
    [m addItem:[self menuItem:@"Help.About"]];
    [m addItem:[NSMenuItem separatorItem]];
    [m addItem:[self menuItem:@"Tools.Options"]];
    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* services = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
    NSMenu* sm = [[NSMenu alloc] initWithTitle:@"Services"];
    services.submenu = sm;
    NSApp.servicesMenu = sm;
    [m addItem:services];
    [m addItem:[NSMenuItem separatorItem]];
    [m addItemWithTitle:@"Hide GRIDEm" action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [m addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [m addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [m addItem:[NSMenuItem separatorItem]];
    [m addItem:[self menuItem:@"File.Exit"]];
    return m;
}

- (NSMenu*)buildMainMenu
{
    NSMenu* bar = [[NSMenu alloc] initWithTitle:@"Main"];
    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"GRIDEm" action:nil keyEquivalent:@""];
    appItem.submenu = [self appMenu];
    [bar addItem:appItem];

    NSMutableArray<NSMenu*>* stack = [NSMutableArray array];
    NSMenu* current = nil;
    for (const char* entry : kMenuLayout)
    {
        NSString* e = Str(entry);
        unichar first = [e characterAtIndex:0];
        if (first == '#')
        {
            NSMenu* m = nil;
            [bar addItem:[self topItem:[e substringFromIndex:1] menu:&m]];
            [stack removeAllObjects];
            current = m;
            if ([e isEqualToString:@"#Window"]) NSApp.windowsMenu = m;
            if ([e isEqualToString:@"#Help"]) NSApp.helpMenu = m;
            continue;
        }
        if (first == '>')
        {
            NSString* title = [e substringFromIndex:1];
            NSMenuItem* sub = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
            NSMenu* sm = [[NSMenu alloc] initWithTitle:title];
            sub.submenu = sm;
            [current addItem:sub];
            [stack addObject:current];
            current = sm;
            continue;
        }
        if (first == '<')
        {
            current = stack.lastObject;
            [stack removeLastObject];
            continue;
        }
        if ([e isEqualToString:@"-"]) { [current addItem:[NSMenuItem separatorItem]]; continue; }
        if (first == '@')
        {
            NSString* what = [e substringFromIndex:1];
            if ([what isEqualToString:@"recentFiles"] || [what isEqualToString:@"recentWorkspaces"]
                || [what isEqualToString:@"toolbars"] || [what isEqualToString:@"externalTools"])
            {
                NSString* title = [what isEqualToString:@"recentFiles"] ? @"Open Recent File"
                                : [what isEqualToString:@"recentWorkspaces"] ? @"Open Recent Workspace"
                                : [what isEqualToString:@"toolbars"] ? @"Toolbars" : @"Run External Tool";
                NSMenuItem* sub = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
                sub.submenu = [[NSMenu alloc] initWithTitle:title];
                sub.tag = [what isEqualToString:@"recentFiles"] ? kGRRecentFilesTag
                        : [what isEqualToString:@"recentWorkspaces"] ? kGRRecentWorkspacesTag
                        : [what isEqualToString:@"toolbars"] ? kGRToolbarsMenuTag : kGRExternalToolsTag;
                [current addItem:sub];
            }
            else if ([what isEqualToString:@"minimize"])
                [current addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
            else if ([what isEqualToString:@"zoom"])
                [current addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
            else if ([what isEqualToString:@"front"])
                [current addItemWithTitle:@"Bring All to Front" action:@selector(arrangeInFront:) keyEquivalent:@""];
            continue;
        }
        [current addItem:[self menuItem:e]];
    }
    return bar;
}

- (void)refreshMenu:(NSMenu*)menu
{
    for (NSMenuItem* item in menu.itemArray)
    {
        if (item.submenu != nil) { [self refreshMenu:item.submenu]; continue; }
        GRCommand* c = [self command:item.representedObject];
        if (c == nil) continue;
        item.title = c.title;
        [self applyKeyOf:c toItem:item];
    }
}

// ---- chords ------------------------------------------------------------------
- (BOOL)isChordStart:(NSString*)keyText
{
    for (GRCommand* c in ordered)
    {
        if (![c isChord]) continue;
        NSRange comma = [c.shortcutText rangeOfString:@"," options:NSBackwardsSearch];
        if ([[c.shortcutText substringToIndex:comma.location] caseInsensitiveCompare:keyText] == NSOrderedSame) return YES;
    }
    return NO;
}

- (GRCommand*)chordCommand:(NSString*)first second:(NSString*)second plainSecond:(NSString*)plain
{
    for (GRCommand* c in ordered)
    {
        if (![c isChord]) continue;
        NSRange comma = [c.shortcutText rangeOfString:@"," options:NSBackwardsSearch];
        NSString* f = [c.shortcutText substringToIndex:comma.location];
        NSString* s = [c.shortcutText substringFromIndex:comma.location + 1];
        if ([f caseInsensitiveCompare:first] != NSOrderedSame) continue;
        // "Ctrl+K,C" also answers Ctrl+K then Ctrl+C, as GRIDEd's FindChord does
        if ([s caseInsensitiveCompare:second] == NSOrderedSame || [s caseInsensitiveCompare:plain] == NSOrderedSame) return c;
    }
    return nil;
}

- (void)startChordMonitor:(void (^)(NSString* status))status
{
    [self stopChordMonitor];
    __weak GRCommandRegistry* weakSelf = self;
    void (^tell)(NSString*) = [status copy];
    chordMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event) {
        GRCommandRegistry* me = weakSelf;
        if (me == nil) return event;
        NSString* key = [GRCommandRegistry keyTextOfEvent:event];
        if (me->pendingChord != nil)
        {
            NSString* first = me->pendingChord;
            me->pendingChord = nil;
            NSString* plain = key;
            NSRange lastPlus = [key rangeOfString:@"+" options:NSBackwardsSearch];
            if (lastPlus.location != NSNotFound && lastPlus.location + 1 < key.length)
                plain = [key substringFromIndex:lastPlus.location + 1];
            GRCommand* c = [me chordCommand:first second:key plainSecond:plain];
            if (tell) tell(@"");
            if (c != nil)
            {
                [me execute:c.commandId];
                return nil;
            }
            if (tell) tell([NSString stringWithFormat:@"The key combination (%@, %@) is not a command.", first, key]);
            return nil;
        }
        if ([me isChordStart:key])
        {
            me->pendingChord = key;
            if (tell) tell([NSString stringWithFormat:@"(%@) was pressed. Waiting for second key of chord...", key]);
            return nil;
        }
        return event;
    }];
}

- (void)stopChordMonitor
{
    if (chordMonitor != nil) [NSEvent removeMonitor:chordMonitor];
    chordMonitor = nil;
    pendingChord = nil;
}
@end
