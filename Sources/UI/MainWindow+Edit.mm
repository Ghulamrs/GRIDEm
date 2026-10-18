// =============================================================================
//  MainWindow+Edit.mm - Edit, View, Window and Help menus; Find in Files
//  (GRIDEd's UI/MainForm.Edit.cpp)
//
//  Undo, the clipboard, Select All, Find and Replace in the document are the
//  Mac's own (the responder chain and the editor's Find bar, NSTextFinder);
//  Find in Files - GRIDEd's FindReplaceDialog in its third mode - is the sheet
//  below, with GRIDEd's scopes and its Find All / Replace All.
// =============================================================================
#import "MainWindow.h"
#import "Common.h"
#import "Commands.h"
#import "HelpWindow.h"
#import "Editor/SourceDocument.h"
#import "Editor/CodeView.h"
#import "Panes/PaneHost.h"
#import "Panes/FindResultsView.h"
#import "Panes/OutputView.h"
#import "Dialogs/DialogKit.h"
#import "Dialogs/Sheet.h"
#import "Dialogs/GoToLinePopover.h"
#import "Dialogs/WindowsSheet.h"
#include "../Core/StringUtil.h"
#include "../Core/TextFile.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include <regex>

using namespace GRIDEm;

typedef NS_ENUM(NSInteger, GRFindScope) { GRFindCurrentDocument = 0, GRFindOpenDocuments, GRFindWorkspace };

// -----------------------------------------------------------------------------
//  the Find in Files sheet
// -----------------------------------------------------------------------------
@interface GRFindInFilesSheet : GRSheet
@property (nonatomic, readonly) NSString* pattern;
@property (nonatomic, readonly) NSString* replacement;
@property (nonatomic, readonly) GRFindScope scope;
@property (nonatomic, readonly) BOOL matchCase;
@property (nonatomic, readonly) BOOL wholeWord;
@property (nonatomic, readonly) BOOL useRegex;
@property (nonatomic, readonly) BOOL replaceAll;         // Replace All was pressed, not Find All
- (instancetype)initWithText:(NSString*)text;
@end

@implementation GRFindInFilesSheet
{
    NSTextField* _find;
    NSTextField* _replace;
    NSPopUpButton* _scope;
    NSButton* _case;
    NSButton* _word;
    NSButton* _regex;
}

- (instancetype)initWithText:(NSString*)text
{
    self = [super initWithTitle:@"Find in Files" size:NSMakeSize(520, 300)];
    if (self == nil) return nil;
    GRForm* form = [[GRForm alloc] init];
    _find = [form addText:@"Find what" value:GROr(text, @"")];
    _replace = [form addText:@"Replace with" value:@""];
    _scope = [form addPopup:@"Look in" items:@[ @"Current Document", @"All Open Documents", @"Entire Workspace" ]
                   selected:@"Entire Workspace"];
    _case = [form addCheck:@"Match case" on:NO];
    _word = [form addCheck:@"Match whole word" on:NO];
    _regex = [form addCheck:@"Use regular expressions (ECMAScript)" on:NO];
    [self setContentView:form.view];
    [self setOkTitle:@"Find All"];
    __weak GRFindInFilesSheet* weakSelf = self;
    [self addButton:@"Replace All" action:^{
        GRFindInFilesSheet* me = weakSelf;
        if (me == nil || me->_find.stringValue.length == 0) return;
        me->_replaceAll = YES;
        [me finish:YES];
    }];
    return self;
}

- (BOOL)accept
{
    if (_find.stringValue.length == 0) { [self showMessage:@"Type the text to find."]; return NO; }
    return YES;
}

- (NSString*)pattern { return _find.stringValue; }
- (NSString*)replacement { return _replace.stringValue; }
- (GRFindScope)scope { return (GRFindScope)MAX(0, _scope.indexOfSelectedItem); }
- (BOOL)matchCase { return _case.state == NSControlStateValueOn; }
- (BOOL)wholeWord { return _word.state == NSControlStateValueOn; }
- (BOOL)useRegex { return _regex.state == NSControlStateValueOn; }
@end

// the regular expression a search builds (GRIDEd's FindOptions::Build); false, with why, when it does not compile
static bool BuildRegex(const std::string& pattern, bool matchCase, bool wholeWord, bool useRegex, std::regex& out, std::string& why)
{
    std::string p;
    if (useRegex) p = pattern;
    else
        for (char c : pattern)
        {
            if (std::string("\\^$.|?*+()[]{}").find(c) != std::string::npos) p += '\\';
            p += c;
        }
    if (wholeWord) p = "\\b(?:" + p + ")\\b";
    try
    {
        std::regex::flag_type f = std::regex::ECMAScript;
        if (!matchCase) f |= std::regex::icase;
        out = std::regex(p, f);
    }
    catch (const std::regex_error& e) { why = std::string("not a regular expression: ") + e.what(); return false; }
    return true;
}

// -----------------------------------------------------------------------------
@implementation GRMainWindowController (Edit)

- (NSArray<NSString*>*)filesInScope:(GRFindScope)scope
{
    NSMutableArray<NSString*>* files = [NSMutableArray array];
    if (scope == GRFindCurrentDocument)
    {
        GRSourceDocument* d = self.editorArea.activeDocument;
        if (d.path != nil) [files addObject:d.path];
    }
    else if (scope == GRFindOpenDocuments)
    {
        for (GRSourceDocument* d in self.editorArea.documents) if (d.path != nil) [files addObject:d.path];
    }
    else if (self.session.workspace)
    {
        for (const Model::GrideProjectPtr& p : self.session.workspace->Projects)
            for (const std::string& f : p->AllFiles())
            {
                NSString* s = GRNS(f);
                if (Platform::Paths::IsFile(f) && ![files containsObject:s]) [files addObject:s];
            }
    }
    return files;
}

- (void)editFindInFiles
{
    GRCodeView* v = self.editorArea.activeCodeView;
    NSString* selected = v != nil ? [v.string substringWithRange:v.selectedRange] : @"";
    if ([selected rangeOfString:@"\n"].location != NSNotFound) selected = @"";
    GRFindInFilesSheet* sheet = [[GRFindInFilesSheet alloc] initWithText:selected];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        std::regex re;
        std::string why;
        if (!BuildRegex(GRStd(sheet.pattern), sheet.matchCase, sheet.wholeWord, sheet.useRegex, re, why))
        {
            GRShowMessage(me.window, @"Find in Files", GRNS(why));
            return;
        }
        if (sheet.replaceAll) [me replaceAll:sheet regex:re];
        else [me findAll:sheet regex:re];
    }];
}

- (void)findAll:(GRFindInFilesSheet*)sheet regex:(const std::regex&)re
{
    NSArray<NSString*>* files = [self filesInScope:sheet.scope];
    [self.findResultsView begin:sheet.pattern];
    NSInteger withHits = 0;
    for (NSString* f in files)
    {
        GRSourceDocument* d = [self.editorArea documentForPath:f];
        std::string text;
        if (d != nil) text = GRStd(d.text);
        else
        {
            Core::TextEncoding enc;
            std::string error;
            if (!Core::TextFile::Read(GRStd(f), text, enc, error)) continue;
        }
        std::vector<std::string> lines = Core::StringUtil::SplitLines(text);
        BOOL hit = NO;
        for (size_t i = 0; i < lines.size(); i++)
            if (std::regex_search(lines[i], re))
            {
                [self.findResultsView addFile:f line:(NSInteger)i + 1 text:GRNS(lines[i])];
                hit = YES;
            }
        if (hit) withHits++;
    }
    [self.findResultsView end:withHits];
    [self showPane:@"FindResults"];
    [self setStatus:[NSString stringWithFormat:@"Searched %lu file(s).", (unsigned long)files.count]];
}

- (void)replaceAll:(GRFindInFilesSheet*)sheet regex:(const std::regex&)re
{
    NSArray<NSString*>* files = [self filesInScope:sheet.scope];
    std::string repl = GRStd(sheet.replacement);
    if (!sheet.useRegex) repl = Core::StringUtil::Replace(repl, "$", "$$");
    std::regex rx = re;          // the block may run after this method returned (the question below)
    void (^run)(void) = ^{
        int total = 0, changed = 0;
        for (NSString* f in files)
        {
            GRSourceDocument* d = [self.editorArea documentForPath:f];
            int n = 0;
            if (d != nil)
            {
                std::string text = GRStd(d.text);
                n = (int)std::distance(std::sregex_iterator(text.cbegin(), text.cend(), rx), std::sregex_iterator());
                if (n > 0)
                {
                    // through the editor, so Undo takes it back (GRIDEd's editor ReplaceAll)
                    NSString* now = GRNS(std::regex_replace(text, rx, repl));
                    GRCodeView* v = [self.editorArea codeViewsFor:d].firstObject;
                    NSRange all = NSMakeRange(0, d.textStorage.length);
                    if (v != nil && [v shouldChangeTextInRange:all replacementString:now])
                    {
                        [v.textStorage replaceCharactersInRange:all withString:now];
                        [v didChangeText];
                    }
                    else d.text = now;
                }
            }
            else
            {
                // written back in the file's own encoding (Windows-1252, UTF-16, BOM or not)
                std::string text, error;
                Core::TextEncoding enc;
                if (!Core::TextFile::Read(GRStd(f), text, enc, error))
                {
                    [self.outputView append:GRNS("Replace All: cannot read " + GRStd(f) + ": " + error) source:@"Tools"];
                    continue;
                }
                n = (int)std::distance(std::sregex_iterator(text.cbegin(), text.cend(), rx), std::sregex_iterator());
                if (n > 0 && !Core::TextFile::Write(GRStd(f), std::regex_replace(text, rx, repl), enc, error))
                {
                    [self.outputView append:GRNS("Replace All: cannot write " + GRStd(f) + ": " + error) source:@"Tools"];
                    n = 0;
                }
            }
            if (n > 0) { total += n; changed++; }
        }
        [self setStatus:[NSString stringWithFormat:@"%d occurrence(s) replaced in %d file(s).", total, changed]];
    };
    if (sheet.scope == GRFindWorkspace)
    {
        NSString* q = [NSString stringWithFormat:@"Replace in all %lu workspace file(s)? Files that are not open are changed on disk.",
                                                 (unsigned long)files.count];
        GRAskYesNo(self.window, @"Replace All", q, ^(BOOL yes) { if (yes) run(); });
        return;
    }
    run();
}

- (void)editGoToLine
{
    GRCodeView* v = self.editorArea.activeCodeView;
    if (v == nil) return;
    [GRGoToLinePopover showFrom:v current:[v currentLine] max:[v lineCount] done:^(NSInteger line) {
        if (line > 0) [v goToLine:line];
    }];
}

// the word at the caret, found where it is defined in the workspace's sources: a function, a type, a macro
- (void)editJumpToDefinition
{
    GRCodeView* v = self.editorArea.activeCodeView;
    if (v == nil) return;
    NSString* s = v.string;
    NSRange sel = v.selectedRange;
    NSCharacterSet* ident = [NSCharacterSet characterSetWithCharactersInString:
                             @"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"];
    NSUInteger b = sel.location, e = NSMaxRange(sel);
    while (b > 0 && [ident characterIsMember:[s characterAtIndex:b - 1]]) b--;
    while (e < s.length && [ident characterIsMember:[s characterAtIndex:e]]) e++;
    if (e <= b) { [self setStatus:@"Put the caret on a name to jump to its definition"]; return; }
    std::string word = GRStd([s substringWithRange:NSMakeRange(b, e - b)]);
    std::regex fn("^[A-Za-z_][\\w\\s\\*&:<>,]*\\b" + word + "\\s*\\([^;]*$");
    std::regex macro("^\\s*#\\s*define\\s+" + word + "\\b");
    std::regex type("\\b(struct|class|union|enum|typedef)\\b[^;(]*\\b" + word + "\\b");
    NSMutableArray<NSString*>* files = [[self filesInScope:GRFindWorkspace] mutableCopy];
    GRSourceDocument* d = self.editorArea.activeDocument;
    if (d.path != nil && ![files containsObject:d.path]) [files insertObject:d.path atIndex:0];
    for (NSString* f in files)
    {
        std::string text, error;
        Core::TextEncoding enc;
        GRSourceDocument* open = [self.editorArea documentForPath:f];
        if (open != nil) text = GRStd(open.text);
        else if (!Core::TextFile::Read(GRStd(f), text, enc, error)) continue;
        std::vector<std::string> lines = Core::StringUtil::SplitLines(text);
        for (size_t i = 0; i < lines.size(); i++)
            if (std::regex_search(lines[i], macro) || std::regex_search(lines[i], fn) || std::regex_search(lines[i], type))
            {
                [self openDocument:f line:(NSInteger)i + 1];
                return;
            }
    }
    [self setStatus:[NSString stringWithFormat:@"No definition of %@ found in the workspace", GRNS(word)]];
}

// converts the indentation to the style chosen in Settings (tabs or spaces)
- (void)editTabify
{
    GRCodeView* v = self.editorArea.activeCodeView;
    if (v == nil) return;
    bool spaces = self.session.settings->Env->InsertSpaces;
    [v convertIndentation:!spaces];
    [self setStatus:spaces ? @"Indentation converted to spaces" : @"Indentation converted to tabs"];
}

// =============================================================================
//  View
// =============================================================================
- (void)viewZoom:(CGFloat)delta
{
    for (GRSourceDocument* d in self.editorArea.documents)
        for (GRCodeView* v in [self.editorArea codeViewsFor:d]) [v zoomBy:delta];
}

- (void)viewZoomReset
{
    CGFloat size = self.session.settings->Env->FontSize;
    for (GRSourceDocument* d in self.editorArea.documents)
        for (GRCodeView* v in [self.editorArea codeViewsFor:d]) [v resetZoom:size];
}

- (void)viewLineNumbers
{
    Settings::EnvironmentSettingsPtr env = self.session.settings->Env;
    env->LineNumbers = !env->LineNumbers;
    [self.editorArea applySettings];
}

- (void)viewWhitespace
{
    Settings::EnvironmentSettingsPtr env = self.session.settings->Env;
    env->ShowWhitespace = !env->ShowWhitespace;
    [self.editorArea applySettings];
}

- (void)viewStatusBar
{
    BOOL show = !self.statusBarShown;
    self.session.settings->Layout->StatusBar = show;
    [self setStatusBarVisible:show];
}

- (void)setThemeNamed:(NSString*)name
{
    self.session.settings->Env->Theme = GRStd(name);
    [self applyTheme];
    [self.session saveSettings];
}

// =============================================================================
//  Window
// =============================================================================
// floats the shown pane of the area that has the keyboard (the bottom area otherwise)
- (void)windowFloat
{
    GRPaneHost* host = self.bottomHost;
    NSResponder* r = self.window.firstResponder;
    if ([r isKindOfClass:[NSView class]])
        for (GRPaneHost* h in @[ self.navigatorHost, self.inspectorHost, self.debugHost, self.bottomHost ])
            if ([(NSView*)r isDescendantOf:h.view]) { host = h; break; }
    if (host.activePane != nil) [host floatPane:host.activePane.paneIdentifier];
}

- (void)windowDock
{
    for (GRPaneHost* h in @[ self.navigatorHost, self.inspectorHost, self.debugHost, self.bottomHost ]) [h dockAll];
}

// collapses (or brings back) every area round the editor
- (void)windowAutoHide
{
    self.autoHidden = !self.autoHidden;
    BOOL show = !self.autoHidden;
    [self setArea:self.sidebarItem shown:show];
    [self setArea:self.inspectorItem shown:show];
    [self setArea:self.bottomItem shown:show];
}

- (void)windowList
{
    NSMutableArray<NSString*>* titles = [NSMutableArray array];
    NSMutableArray<NSString*>* paths = [NSMutableArray array];
    NSArray<GRSourceDocument*>* docs = self.editorArea.documents;
    for (GRSourceDocument* d in docs)
    {
        [titles addObject:d.displayName];
        [paths addObject:GROr(d.path, @"")];
    }
    GRWindowsSheet* sheet = [[GRWindowsSheet alloc] initWithTitles:titles paths:paths];
    __weak GRMainWindowController* weakSelf = self;
    [sheet beginOn:self.window completion:^(BOOL ok) {
        GRMainWindowController* me = weakSelf;
        if (!ok || me == nil) return;
        NSMutableArray<GRSourceDocument*>* chosen = [NSMutableArray array];
        [sheet.selected enumerateIndexesUsingBlock:^(NSUInteger i, BOOL* stop) {
            (void)stop;
            if (i < docs.count) [chosen addObject:docs[i]];
        }];
        for (GRSourceDocument* d in chosen)
        {
            if (sheet.action == GRWindowsActivate) { [me.editorArea activateDocument:d]; break; }
            if (sheet.action == GRWindowsSave) [me.editorArea saveDocument:d];
            if (sheet.action == GRWindowsClose) [me.editorArea closeDocument:d done:^(BOOL closed) { (void)closed; }];
        }
    }];
}

// =============================================================================
//  Help
// =============================================================================
// where the help is looked for: the folder named in Settings, then the app's Resources/docs and
// Resources/help, then docs and help beside the program and above it (GRIDEd's HelpFolders)
- (NSArray<NSString*>*)helpFolders
{
    NSMutableArray<NSString*>* dirs = [NSMutableArray array];
    std::string named = Platform::Environment::Expand(self.session.settings->Env->HelpDirectory);
    if (!named.empty()) [dirs addObject:GRNS(Platform::Paths::ExpandHome(named))];
    std::string res = Platform::Paths::ResourcesDirectory();
    std::string app = Platform::Paths::AppDirectory();
    std::string above = Platform::Paths::DirectoryName(app);
    const std::string candidates[] = {
        Platform::Paths::Combine(res, "docs"), Platform::Paths::Combine(res, "help"), app,
        Platform::Paths::Combine(app, "docs"), Platform::Paths::Combine(app, "help"),
        Platform::Paths::Combine(above, "docs"), Platform::Paths::Combine(above, "help")
    };
    for (const std::string& d : candidates)
        if (![dirs containsObject:GRNS(d)]) [dirs addObject:GRNS(d)];
    return dirs;
}

- (NSString*)findHelpFile:(NSString*)baseName extensions:(NSArray<NSString*>*)extensions
{
    for (NSString* dir in [self helpFolders])
        for (NSString* ext in extensions)
        {
            NSString* f = [[dir stringByAppendingPathComponent:baseName] stringByAppendingPathExtension:ext];
            if ([[NSFileManager defaultManager] fileExistsAtPath:f]) return f;
        }
    return nil;
}

- (void)openHelpFile:(NSString*)baseName
{
    NSString* f = [self findHelpFile:baseName extensions:@[ @"html", @"htm", @"pdf", @"md", @"txt" ]];
    if (f != nil) { [[GRHelpWindow shared] showFile:f]; return; }
    NSString* text = [NSString stringWithFormat:@"The reference document is not installed yet.\n\nPlace %@.html (or .pdf / .md / .txt) "
                                                "in one of:\n%@\n\nThe first folder can be set in Settings > General.",
                                                baseName, [[self helpFolders] componentsJoinedByString:@"\n"]];
    GRShowMessage(self.window, @"Help", text);
}

- (void)helpReleaseNotes
{
    NSString* f = [self findHelpFile:@"ReleaseNotes" extensions:@[ @"txt", @"md" ]];
    NSString* text = f != nil ? [NSString stringWithContentsOfFile:f encoding:NSUTF8StringEncoding error:nil] : nil;
    if (text == nil)
        text = @"GRIDEm 1.0\n==========\n\n"
                "* GRIDE native on macOS: AppKit, the Mac's menus, toolbar, Settings window and Dark Mode\n"
                "* Languages C90, C++11, Shalimar; targets macOS, TMS320C6747, Windows and Linux (to assembly)\n"
                "* GRIDEd's files: .pro, .grws, gride.settings.json and toolchain.json, read and written the same way\n"
                "* Read-only Code Composer Studio 7.4 / 5.5 projects and Visual Studio solutions\n"
                "* RIDE's build engine: compile, assemble, link, with error reading into Issues\n"
                "* Run in the Output pane with an input line; sim6747 / vm6747 / Verify on the C6747\n"
                "* Debugging with lldb (macOS) and sim6747 / vm6747 (C6747): Locals, Watch, Call Stack,\n"
                "  Registers, Memory, Disassembly\n"
                "* C <-> Shalimar conversion through c2s\n";
    GRTextSheet* sheet = [[GRTextSheet alloc] initWithTitle:@"Release Notes" text:text mono:YES];
    [sheet beginOn:self.window completion:^(BOOL ok) { (void)ok; }];
}
@end
