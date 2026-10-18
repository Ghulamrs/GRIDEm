// =============================================================================
//  SettingsWindow.mm - see SettingsWindow.h (the window, the page base and the
//  plain pages; Keyboard, Toolchains and Definitions have files of their own)
// =============================================================================
#import "SettingsWindow.h"
#import "CompilerOptionsView.h"
#import "DialogKit.h"
#import "../Theme.h"
#include "../../Core/StringUtil.h"
#include "../../Services/RideToolchain.h"
#include "../../Settings/ToolchainDefinitions.h"
#include "../../Settings/ToolchainSettings.h"
#include <stdexcept>

using namespace GRIDEm;

// =============================================================================
//  the page base
// =============================================================================
@implementation GRSettingsPage
{
    NSString* _pageId;
    NSString* _symbolName;
    Settings::GrideSettingsPtr _work;
}

- (instancetype)initWithId:(NSString*)pageId title:(NSString*)title symbol:(NSString*)symbol
                  settings:(const Settings::GrideSettingsPtr&)work
{
    if ((self = [super initWithNibName:nil bundle:nil]))
    {
        _pageId = [pageId copy];
        _symbolName = [symbol copy];
        _work = work;
        self.title = title;
    }
    return self;
}

- (NSString*)pageId { return _pageId; }
- (NSString*)symbolName { return _symbolName; }
- (Settings::GrideSettingsPtr)work { return _work; }

- (NSView*)makeContent { return [[NSView alloc] initWithFrame:NSZeroRect]; }
- (void)commit {}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 780, 580)];
    NSView* content = [self makeContent];
    content.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton* ok = [NSButton buttonWithTitle:@"OK" target:nil action:@selector(okPressed:)];
    ok.keyEquivalent = @"\r";
    NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:nil action:@selector(cancelPressed:)];
    cancel.keyEquivalent = @"\033";
    // the window controller is in the responder chain; aim at it outright when we know it
    ok.target = self.owner;
    cancel.target = self.owner;
    NSStackView* bar = [NSStackView stackViewWithViews:@[cancel, ok]];
    bar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    bar.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:content];
    [root addSubview:bar];
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [content.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [content.topAnchor constraintEqualToAnchor:root.topAnchor],
        [bar.topAnchor constraintEqualToAnchor:content.bottomAnchor constant:10],
        [bar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-16],
        [bar.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-14],
    ]];
    self.view = root;
    self.preferredContentSize = NSMakeSize(780, 580);
}
@end

// =============================================================================
//  General
// =============================================================================
@implementation GRGeneralPage
{
    NSPopUpButton* _theme;
    NSButton* _reopen;
    NSButton* _saveBefore;
    NSButton* _jumpFirst;
    NSButton* _askNative;
    NSButton* _runInIde;
    GRPathField* _location;
    GRPathField* _help;
}

- (NSView*)makeContent
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Appearance"];
    _theme = [form addPopup:@"Colour theme" items:[GRTheme names] selected:GRNS(env->Theme)];
    [form addSection:@"Start-up and build"];
    _reopen = [form addCheck:@"Reopen the last workspace at start-up" on:env->ReopenLastWorkspace];
    _saveBefore = [form addCheck:@"Save all files before building" on:env->SaveBeforeBuild];
    _jumpFirst = [form addCheck:@"Go to the first error when a build fails" on:env->JumpToFirstError];
    _askNative = [form addCheck:@"Offer the vendor's assembler and linker when the project's own fail" on:env->AskNativeTools];
    _runInIde = [form addCheck:@"Run programs in the Output pane, with an input line (off: Terminal)" on:env->RunInIde];
    [form addSection:@"Locations"];
    _location = [[GRPathField alloc] initFolder:YES status:YES];
    _location.value = GRNS(env->DefaultProjectLocation);
    [form addRow:@"Default project location" view:_location];
    _help = [[GRPathField alloc] initFolder:YES status:YES];
    _help.value = GRNS(env->HelpDirectory);
    [form addRow:@"Help / reference folder" view:_help];
    [form addSection:@"Settings file"];
    NSTextField* path = [form addText:(self.work->IsPortable ? @"Portable (beside GRIDEm.app)"
                                                             : @"User (~/Library/Application Support/GRIDE)")
                                value:GRNS(self.work->FilePath)];
    path.editable = NO;
    return form.view;
}

- (void)commit
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    env->Theme = GRStd(_theme.titleOfSelectedItem);
    env->ReopenLastWorkspace = _reopen.state == NSControlStateValueOn;
    env->SaveBeforeBuild = _saveBefore.state == NSControlStateValueOn;
    env->JumpToFirstError = _jumpFirst.state == NSControlStateValueOn;
    env->AskNativeTools = _askNative.state == NSControlStateValueOn;
    env->RunInIde = _runInIde.state == NSControlStateValueOn;
    env->DefaultProjectLocation = GRStd(_location.value);
    env->HelpDirectory = GRStd(_help.value);
}
@end

// =============================================================================
//  Editor
// =============================================================================
@implementation GREditorPage
{
    NSTextField* _tabSize;
    NSButton* _insertSpaces;
    NSButton* _lineNumbers;
    NSButton* _whitespace;
}

- (NSView*)makeContent
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Text editor"];
    _tabSize = [form addNumber:@"Tab size" value:env->TabSize min:1 max:16 decimals:0];
    _insertSpaces = [form addCheck:@"Insert spaces instead of tabs" on:env->InsertSpaces];
    _lineNumbers = [form addCheck:@"Show line numbers" on:env->LineNumbers];
    _whitespace = [form addCheck:@"Show white space" on:env->ShowWhitespace];
    return form.view;
}

- (void)commit
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    int tab = _tabSize.intValue;
    env->TabSize = tab < 1 ? 1 : (tab > 16 ? 16 : tab);
    env->InsertSpaces = _insertSpaces.state == NSControlStateValueOn;
    env->LineNumbers = _lineNumbers.state == NSControlStateValueOn;
    env->ShowWhitespace = _whitespace.state == NSControlStateValueOn;
}
@end

// =============================================================================
//  Fonts & Colours
// =============================================================================
@implementation GRFontsColoursPage
{
    NSComboBox* _font;
    NSTextField* _size;
    NSMutableDictionary<NSString*, NSString*>* _colors;     // token -> "#RRGGBB"
    NSMutableDictionary<NSString*, NSColorWell*>* _wells;
}

- (NSView*)makeContent
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    _colors = [NSMutableDictionary dictionary];
    for (const std::pair<std::string, std::string>& kv : env->SyntaxColors.Items())
        _colors[GRNS(kv.first)] = GRNS(kv.second);
    _wells = [NSMutableDictionary dictionary];
    GRForm* form = [[GRForm alloc] init];
    _font = [[NSComboBox alloc] initWithFrame:NSZeroRect];
    [_font addItemsWithObjectValues:[[NSFontManager sharedFontManager] availableFontFamilies]];
    _font.stringValue = GRNS(env->FontName);
    [form addRow:@"Editor font" view:_font];
    _size = [form addNumber:@"Size (pt)" value:env->FontSize min:6 max:40 decimals:1];
    [form addSection:@"Syntax colours (over the theme)"];
    for (NSString* token in [GRTheme tokenNames])
    {
        NSColorWell* well = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 60, 24)];
        well.identifier = token;
        well.target = self;
        well.action = @selector(colourPicked:);
        [well.widthAnchor constraintEqualToConstant:60].active = YES;
        _wells[token] = well;
        [form addRow:token view:well];
    }
    NSButton* reset = [NSButton buttonWithTitle:@"Reset to theme defaults" target:self action:@selector(resetColours:)];
    [form addRow:@"" view:reset];
    [self updateWells];
    return form.view;
}

- (NSColor*)effective:(NSString*)token
{
    NSString* hex = _colors[token];
    if (hex.length > 0)
    {
        NSColor* c = [GRTheme colorFromHex:hex];
        if (c != nil) return c;
    }
    GRTheme* theme = [[GRTheme themeNamed:GRNS(self.work->Env->Theme)] resolvedFor:[NSApp effectiveAppearance]];
    return [theme colorForTokenName:token];
}

- (void)updateWells
{
    for (NSString* token in _wells) _wells[token].color = [self effective:token];
}

- (void)colourPicked:(NSColorWell*)well
{
    _colors[well.identifier] = [GRTheme hexFromColor:well.color];
}

- (void)resetColours:(id)sender
{
    (void)sender;
    [_colors removeAllObjects];
    [self updateWells];
}

- (void)commit
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    NSString* font = [_font.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (font.length > 0) env->FontName = GRStd(font);
    float size = _size.floatValue;
    env->FontSize = size < 6 ? 6 : (size > 40 ? 40 : size);
    Settings::OrderedStringMap colours;
    // the order the user's file had, then the new ones
    for (const std::pair<std::string, std::string>& kv : env->SyntaxColors.Items())
    {
        NSString* v = _colors[GRNS(kv.first)];
        if (v != nil) colours.Set(kv.first, GRStd(v));
    }
    for (NSString* token in [GRTheme tokenNames])
    {
        NSString* v = _colors[token];
        if (v != nil && !colours.Has(GRStd(token))) colours.Set(GRStd(token), GRStd(v));
    }
    env->SyntaxColors = colours;
}
@end

// =============================================================================
//  RIDE Paths (RIDE's settings.json: root, include / lib, vcvars, ti / tilib,
//  includes / libraries, compiler, the C6747 debugger)
// =============================================================================
@implementation GRRidePathsPage
{
    GRPathField* _ride;
    GRPathField* _include;
    GRPathField* _lib;
    GRPathField* _vcvars;
    GRPathField* _ti;
    GRPathField* _tilib;
    NSTextView* _includes;
    NSTextView* _libraries;
    NSPopUpButton* _compiler;
    NSPopUpButton* _c6747;
}

- (NSView*)makeContent
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"RIDE's installation"];
    _ride = [[GRPathField alloc] initFolder:YES status:YES];
    _ride.value = GRNS(env->RideRoot);
    [form addRow:@"RIDE root or bin" view:_ride];
    [form addNote:@"Where c90, cpp11, shalimar, masm, link, asm6x, lnk6x, vm6747, sim6747 and lib/rts6x-tms6747 are "
                   "looked for when a toolchain names a tool without a folder. Empty: inside GRIDEm.app, then PATH."];

    std::string root = Services::RideToolchain::InstallRoot(Settings::ToolchainSettings::RideRoot);
    NSString* rootText = root.empty() ? @" (none found)" : [NSString stringWithFormat:@" (%@)", GRNS(root)];
    [form addSection:@"RIDE's headers (settings.json include / lib)"];
    _include = [[GRPathField alloc] initFolder:YES status:YES];
    _include.baseDir = GRNS(root);
    _include.value = GRNS(env->RideIncludeDir);
    [form addRow:@"cpp11's headers (include)" view:_include];
    _lib = [[GRPathField alloc] initFolder:YES status:YES];
    _lib.baseDir = GRNS(root);
    _lib.value = GRNS(env->RideLibDir);
    [form addRow:@"c90's headers (lib)" view:_lib];
    [form addNote:[NSString stringWithFormat:@"Every cpp11 compile is given the include directory and every c90 compile the "
                   "lib directory, after the project's own. Relative to RIDE's installation%@; empty: the directory of "
                   "that name there.", rootText]];

    [form addSection:@"Visual Studio (settings.json vcvars)"];
    _vcvars = [[GRPathField alloc] initFolder:NO status:NO];
    _vcvars.value = GRNS(env->Vcvars);
    [form addRow:@"vcvars64.bat" view:_vcvars];
    [form addNote:@"Windows only: kept so the settings file reads the same in GRIDEd. Not used on the Mac."];

    [form addSection:@"TI's C6000 compiler (settings.json ti / tilib)"];
    _ti = [[GRPathField alloc] initFolder:YES status:YES];
    _ti.value = GRNS(env->TiCompilerDir);
    [form addRow:@"TI compiler directory" view:_ti];
    _tilib = [[GRPathField alloc] initFolder:YES status:YES];
    _tilib.value = GRNS(env->TiLibDir);
    [form addRow:@"TI library directory" view:_tilib];
    std::string detected = Services::RideToolchain::DetectedTiDir();
    NSString* roots = GRNS(Core::StringUtil::Join(", ", Settings::ToolchainDefinitions::Current()->Ccs.TiRoots));
    [form addNote:[NSString stringWithFormat:@"The directory with bin/lnk6x (ti-cgt-c6000_x.y.z). Named, a TMS320C6747 "
                   "build links against TI's runtime (rts6740_elf_eh.lib where it is, in lib or in the library directory) "
                   "instead of RTS6x, and TI's lnk6x stands in for a linker that is not there. Empty: RTS6x inside "
                   "GRIDEm; the newest CCS compiler found under %@ when RTS6x is not there%@.", roots,
                   detected.empty() ? @" (none found now)"
                                    : [NSString stringWithFormat:@" (found: %@)", GRNS(detected)]]];

    [form addSection:@"Shared (settings.json includes / libraries)"];
    _includes = [form addLines:@"Header directories every compile searches after the project's own (one per line)"
                          text:GRJoinLines(env->SharedIncludes) height:60 mono:YES];
    _libraries = [form addLines:@"Libraries every host link takes (one per line)"
                           text:GRJoinLines(env->SharedLibraries) height:60 mono:YES];

    [form addSection:@"Defaults"];
    NSMutableArray* words = [NSMutableArray arrayWithObject:@"auto"];
    for (Services::RideTool t : Services::RideToolchain::Compilers()) [words addObject:GRNS(Services::RideToolchain::Word(t))];
    _compiler = [form addPopup:@"Compiler a new project starts with" items:words selected:GRNS(env->DefaultCompiler)];
    _c6747 = [form addPopup:@"TMS320C6747 debugger" items:@[@"auto", @"sim6747", @"vm6747"] selected:GRNS(env->C6747Debugger)];
    [form addNote:@"auto: sim6747 on this build's .out when it has a line table, else vm6747 on the assembly (RIDE). "
                   "RIDE_DEBUG_SIM6747=1 / RIDE_DEBUG_VM6747=1 in the environment at start win."];
    return form.view;
}

- (void)commit
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    env->RideRoot = GRStd(_ride.value);
    Settings::ToolchainSettings::RideRoot = env->RideRoot;
    env->RideIncludeDir = GRStd(_include.value);
    env->RideLibDir = GRStd(_lib.value);
    env->Vcvars = GRStd(_vcvars.value);
    env->TiCompilerDir = GRStd(_ti.value);
    env->TiLibDir = GRStd(_tilib.value);
    env->SharedIncludes = GRSplitLines(_includes.string);
    env->SharedLibraries = GRSplitLines(_libraries.string);
    env->DefaultCompiler = GRStd(_compiler.titleOfSelectedItem);
    env->C6747Debugger = GRStd(_c6747.titleOfSelectedItem);
}
@end

// =============================================================================
//  Converters
// =============================================================================
@implementation GRConvertersPage
{
    GRPathField* _toShl;
    GRPathField* _toC;
    NSTextField* _cmd;
    NSButton* _open;
}

- (NSView*)makeContent
{
    Settings::ConverterSettingsPtr c = self.work->Converter;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"C <-> Shalimar"];
    _toShl = [[GRPathField alloc] initFolder:NO status:YES];
    _toShl.value = GRNS(c->CToShalimar);
    [form addRow:@"C -> Shalimar tool" view:_toShl];
    _toC = [[GRPathField alloc] initFolder:NO status:YES];
    _toC.value = GRNS(c->ShalimarToC);
    [form addRow:@"Shalimar -> C tool" view:_toC];
    _cmd = [form addMono:@"Command template" value:GRNS(c->CommandTemplate)];
    [form addNote:@"Leave the tools and the template empty to use toolchain.json's converter (c2s, --to-shalimar / "
                   "--to-c), found inside GRIDEm.app or as its lookup says. Template macros: $(Tool) $(Input) $(Output)"];
    _open = [form addCheck:@"Open the converted file in the editor" on:c->OpenResult];
    return form.view;
}

- (void)commit
{
    Settings::ConverterSettingsPtr c = self.work->Converter;
    c->CToShalimar = GRStd(_toShl.value);
    c->ShalimarToC = GRStd(_toC.value);
    c->CommandTemplate = GRStd(_cmd.stringValue);
    c->OpenResult = _open.state == NSControlStateValueOn;
}
@end

// =============================================================================
//  CCS (GRIDEd's Projects page, its Code Composer Studio part)
// =============================================================================
@implementation GRCcsPage
{
    NSButton* _enabled;
    GRPathField* _root;
    GRPathField* _cg;
}

- (NSView*)makeContent
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"Code Composer Studio 7.4 / 5.5 projects"];
    _enabled = [form addCheck:@"Open Code Composer Studio projects (File > Open > CCS Project)" on:env->CcsEnabled];
    _root = [[GRPathField alloc] initFolder:YES status:YES];
    _root.value = GRNS(env->CcsRoot);
    [form addRow:@"CCS installation" view:_root];
    [form addNote:@"RIDE's ccs.root (for example /Applications/ti/ccsv7): ${CG_TOOL_ROOT} is the project's compiler "
                   "version under its tools/compiler. Empty: TI's compiler directory (RIDE Paths), else the directory below."];
    _cg = [[GRPathField alloc] initFolder:YES status:YES];
    _cg.value = GRNS(env->CcsToolRoot);
    [form addRow:@"${CG_TOOL_ROOT}" view:_cg];
    [form addNote:@"A compiler directory named outright, used to expand ${CG_TOOL_ROOT} in include paths read from "
                   ".cproject files when no CCS installation is named."];
    return form.view;
}

- (void)commit
{
    Settings::EnvironmentSettingsPtr env = self.work->Env;
    env->CcsEnabled = _enabled.state == NSControlStateValueOn;
    env->CcsRoot = GRStd(_root.value);
    env->CcsToolRoot = GRStd(_cg.value);
}
@end

// =============================================================================
//  Languages - one language at a time, its edits kept while another is shown
// =============================================================================
@implementation GRLanguagesPage
{
    NSPopUpButton* _which;
    NSTextField* _ext;
    NSTextField* _lineC;
    NSTextField* _blockS;
    NSTextField* _blockE;
    NSButton* _pre;
    NSTextView* _keywords;
    NSTextView* _types;
    NSTextField* _note;
    Settings::LanguageSettingsPtr _shown;
}

- (NSView*)makeContent
{
    GRForm* form = [[GRForm alloc] init];
    NSMutableArray* names = [NSMutableArray array];
    for (const Settings::LanguageSettingsPtr& l : self.work->Languages)
        if (l->Lang != Core::Language::LinkerCmd) [names addObject:GRNS(l->Name())];
    _which = [form addPopup:@"Language" items:names selected:nil];
    _which.target = self;
    _which.action = @selector(languageChosen:);
    [form addSection:@"Words and comments"];
    _ext = [form addMono:@"File extensions" value:@""];
    _lineC = [form addMono:@"Line comment" value:@""];
    _blockS = [form addMono:@"Block comment start" value:@""];
    _blockE = [form addMono:@"Block comment end" value:@""];
    _pre = [form addCheck:@"Highlight preprocessor directives (#include ...)" on:NO];
    _keywords = [form addLines:@"Keywords" text:@"" height:150 mono:YES];
    _types = [form addLines:@"Type names" text:@"" height:90 mono:YES];
    _note = [form addNote:@""];
    [self languageChosen:nil];
    return form.view;
}

- (void)capture
{
    if (!_shown) return;
    _shown->Extensions = GRSplitSemicolons(_ext.stringValue);
    _shown->LineComment = Core::StringUtil::Trim(GRStd(_lineC.stringValue));
    _shown->BlockCommentStart = Core::StringUtil::Trim(GRStd(_blockS.stringValue));
    _shown->BlockCommentEnd = Core::StringUtil::Trim(GRStd(_blockE.stringValue));
    _shown->Preprocessor = _pre.state == NSControlStateValueOn;
    _shown->Keywords = GRSplitLines(_keywords.string);
    _shown->Types = GRSplitLines(_types.string);
}

- (void)languageChosen:(id)sender
{
    (void)sender;
    [self capture];
    _shown = self.work->LanguageOf(Core::LanguageInfo::Parse(GRStd(_which.titleOfSelectedItem)));
    if (!_shown) return;
    _ext.stringValue = GRNS(Core::StringUtil::Join(";", _shown->Extensions));
    _lineC.stringValue = GRNS(_shown->LineComment);
    _blockS.stringValue = GRNS(_shown->BlockCommentStart);
    _blockE.stringValue = GRNS(_shown->BlockCommentEnd);
    _pre.state = _shown->Preprocessor ? NSControlStateValueOn : NSControlStateValueOff;
    _keywords.string = GRJoinLines(_shown->Keywords);
    _types.string = GRJoinLines(_shown->Types);
    _note.stringValue = _shown->Lang == Core::Language::Shalimar
        ? @"The Shalimar word list is a placeholder (C-like). Replace it with the real Shalimar keywords." : @"";
}

- (void)commit { [self capture]; }
@end

// =============================================================================
//  Compiler Options (RIDE's installation-level store: what a new project starts with)
// =============================================================================
@implementation GRCompilerOptionsPage
{
    GRCompilerOptionsView* _editor;
}

- (NSView*)makeContent
{
    NSTextField* note = [NSTextField wrappingLabelWithString:
        @"RIDE's Compiler Options as the installation sets them: what a new project starts with. A project's own are "
         "under Project > Compiler Options; a build takes the project's."];
    note.textColor = [NSColor secondaryLabelColor];
    note.preferredMaxLayoutWidth = 720;
    Settings::TargetDefinitionPtr host = Settings::ToolchainDefinitions::Current()->HostTarget();
    Core::Target t = host ? Core::TargetInfo::Parse(host->IdeTarget) : Core::Target::MacOS;
    _editor = [[GRCompilerOptionsView alloc] initWithStore:self.work->CompilerOptions config:@"Debug" target:t readOnly:NO];
    NSStackView* stack = [NSStackView stackViewWithViews:@[note, _editor]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.edgeInsets = NSEdgeInsetsMake(14, 14, 0, 14);
    [_editor.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-28].active = YES;
    [_editor setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationVertical];
    return stack;
}

- (void)commit { [_editor commit]; }
@end

// =============================================================================
//  the window
// =============================================================================
static GRSettingsWindow* shownSettings = nil;

@implementation GRSettingsWindow
{
    Settings::GrideSettingsPtr _work;
    NSTabViewController* _tabs;
    NSMutableArray<GRSettingsPage*>* _pages;
    GRSettingsApplied _applied;
}

+ (void)showWithSettings:(const Settings::GrideSettingsPtr&)current startPage:(NSString*)startPage
                 applied:(GRSettingsApplied)applied
{
    if (shownSettings != nil)
    {
        [shownSettings selectPage:startPage];
        [shownSettings.window makeKeyAndOrderFront:nil];
        return;
    }
    shownSettings = [[GRSettingsWindow alloc] initWithSettings:current applied:applied];
    [shownSettings selectPage:startPage];
    [shownSettings.window center];
    [shownSettings showWindow:nil];
}

- (instancetype)initWithSettings:(const Settings::GrideSettingsPtr&)current applied:(GRSettingsApplied)applied
{
    Settings::GrideSettingsPtr work = current->Clone();
    NSMutableArray<GRSettingsPage*>* pages = [NSMutableArray array];
    [pages addObject:[[GRGeneralPage alloc] initWithId:@"General" title:@"General" symbol:@"gearshape" settings:work]];
    [pages addObject:[[GREditorPage alloc] initWithId:@"Editor" title:@"Editor" symbol:@"text.cursor" settings:work]];
    [pages addObject:[[GRFontsColoursPage alloc] initWithId:@"Fonts" title:@"Fonts & Colours" symbol:@"paintpalette" settings:work]];
    [pages addObject:[[GRKeyboardPage alloc] initWithId:@"Keyboard" title:@"Keyboard" symbol:@"keyboard" settings:work]];
    [pages addObject:[[GRToolchainsPage alloc] initWithId:@"Toolchains" title:@"Toolchains" symbol:@"hammer" settings:work]];
    [pages addObject:[[GRDefinitionsPage alloc] initWithId:@"TCDEF" title:@"Definitions" symbol:@"doc.badge.gearshape" settings:work]];
    [pages addObject:[[GRRidePathsPage alloc] initWithId:@"RidePaths" title:@"RIDE Paths" symbol:@"folder" settings:work]];
    [pages addObject:[[GRConvertersPage alloc] initWithId:@"Converters" title:@"Converters" symbol:@"arrow.left.arrow.right" settings:work]];
    [pages addObject:[[GRCcsPage alloc] initWithId:@"CCS" title:@"CCS" symbol:@"cpu" settings:work]];
    [pages addObject:[[GRLanguagesPage alloc] initWithId:@"Languages" title:@"Languages" symbol:@"character.book.closed" settings:work]];
    [pages addObject:[[GRCompilerOptionsPage alloc] initWithId:@"CompilerOptions" title:@"Compiler Options" symbol:@"slider.horizontal.3" settings:work]];

    NSTabViewController* tabs = [[NSTabViewController alloc] init];
    tabs.tabStyle = NSTabViewControllerTabStyleToolbar;
    tabs.transitionOptions = NSViewControllerTransitionNone;
    for (GRSettingsPage* page in pages)
    {
        NSTabViewItem* item = [NSTabViewItem tabViewItemWithViewController:page];
        item.label = page.title;
        item.identifier = page.pageId;
        item.image = [NSImage imageWithSystemSymbolName:page.symbolName accessibilityDescription:page.title];
        [tabs addTabViewItem:item];
    }
    NSWindow* window = [NSWindow windowWithContentViewController:tabs];
    window.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    window.toolbarStyle = NSWindowToolbarStylePreference;
    window.releasedWhenClosed = NO;
    window.title = @"Settings";
    if ((self = [super initWithWindow:window]))
    {
        _work = work;
        _tabs = tabs;
        _pages = pages;
        _applied = [applied copy];
        for (GRSettingsPage* page in pages) page.owner = self;
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowWillClose:)
                                                     name:NSWindowWillCloseNotification object:window];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)selectPage:(NSString*)pageId
{
    if (pageId.length == 0) return;
    for (NSUInteger i = 0; i < _pages.count; i++)
        if ([_pages[i].pageId isEqualToString:pageId] || [_pages[i].title isEqualToString:pageId])
        {
            _tabs.selectedTabViewItemIndex = (NSInteger)i;
            return;
        }
}

- (void)okPressed:(id)sender
{
    (void)sender;
    [self.window makeFirstResponder:nil];
    // the definitions page last: its reload rebases the toolchain pages' untouched values
    GRSettingsPage* definitions = nil;
    for (GRSettingsPage* page in _pages)
    {
        if (!page.viewLoaded) continue;           // a page never shown has nothing to commit
        if ([page.pageId isEqualToString:@"TCDEF"]) { definitions = page; continue; }
        [page commit];
    }
    [definitions commit];
    try { Settings::SettingsManager::Save(_work); }
    catch (const std::exception& e)
    {
        GRShowMessage(self.window, @"Settings", [@"The settings could not be saved:\n" stringByAppendingString:GRNS(e.what())]);
        return;
    }
    _work->PublishPaths();
    GRSettingsApplied applied = _applied;
    Settings::GrideSettingsPtr result = _work;
    [self.window close];
    if (applied) applied(result);
}

- (void)cancelPressed:(id)sender
{
    (void)sender;
    [self.window close];
}

- (void)windowWillClose:(NSNotification*)note
{
    (void)note;
    if (shownSettings == self) shownSettings = nil;
}
@end
