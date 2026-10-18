// =============================================================================
//  EditorArea.mm - see EditorArea.h
// =============================================================================
#import "EditorArea.h"
#import "../Common.h"
#import "CodeView.h"
#import "Gutter.h"
#import "SourceDocument.h"
#import "TabBar.h"
#import "../Icons.h"
#import "../Theme.h"
#import "Highlighter.h"

@interface GREditorArea () <GRTabBarDelegate, NSTextViewDelegate>
@end

@implementation GREditorArea
{
    NSMutableArray<GRSourceDocument*>* docs;
    GRSourceDocument* active;
    NSMapTable<GRSourceDocument*, NSScrollView*>* primary;    // each document's own view, kept with its scroll position
    NSScrollView* secondary;                                  // the lower pane of a split, for the active document
    GRTabBar* tabBar;
    NSPathControl* jumpBar;
    NSSplitView* split;
    NSTextField* emptyLabel;
    BOOL splitOn;
    BOOL jumpScheduled;
    NSMutableArray* jumpPaths;                                // the folder of each jump bar item, or NSNull
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) [self build];
    return self;
}

- (instancetype)initWithCoder:(NSCoder*)coder
{
    self = [super initWithCoder:coder];
    if (self) [self build];
    return self;
}

- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; }

- (void)build
{
    docs = [NSMutableArray array];
    primary = [NSMapTable mapTableWithKeyOptions:NSPointerFunctionsStrongMemory | NSPointerFunctionsObjectPointerPersonality
                                    valueOptions:NSPointerFunctionsStrongMemory];
    _projectLanguage = GRIDEm::Core::Language::C90;

    tabBar = [[GRTabBar alloc] initWithFrame:NSZeroRect];
    tabBar.delegate = self;
    tabBar.translatesAutoresizingMaskIntoConstraints = NO;

    jumpBar = [[NSPathControl alloc] initWithFrame:NSZeroRect];
    jumpBar.pathStyle = NSPathStyleStandard;
    jumpBar.controlSize = NSControlSizeSmall;
    jumpBar.font = [NSFont systemFontOfSize:11];
    jumpBar.focusRingType = NSFocusRingTypeNone;
    jumpBar.target = self;
    jumpBar.action = @selector(jumpBarClicked:);
    jumpBar.translatesAutoresizingMaskIntoConstraints = NO;

    split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    split.vertical = NO;                       // one pane above the other, as GRIDEd splits
    split.dividerStyle = NSSplitViewDividerStyleThin;
    split.translatesAutoresizingMaskIntoConstraints = NO;

    emptyLabel = [NSTextField labelWithString:@"No Editor"];
    emptyLabel.font = [NSFont systemFontOfSize:20 weight:NSFontWeightLight];
    emptyLabel.textColor = [NSColor tertiaryLabelColor];
    emptyLabel.translatesAutoresizingMaskIntoConstraints = NO;

    [self addSubview:tabBar];
    [self addSubview:jumpBar];
    [self addSubview:split];
    [self addSubview:emptyLabel];
    [NSLayoutConstraint activateConstraints:@[
        [tabBar.topAnchor constraintEqualToAnchor:self.topAnchor],
        [tabBar.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [tabBar.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [tabBar.heightAnchor constraintEqualToConstant:28],
        [jumpBar.topAnchor constraintEqualToAnchor:tabBar.bottomAnchor constant:1],
        [jumpBar.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:6],
        [jumpBar.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-6],
        [jumpBar.heightAnchor constraintEqualToConstant:20],
        [split.topAnchor constraintEqualToAnchor:jumpBar.bottomAnchor constant:1],
        [split.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [split.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [split.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        [emptyLabel.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
        [emptyLabel.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    ]];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(documentState:)
                                                 name:GRSourceDocumentStateNotification object:nil];
    [self showEmpty];
}

- (void)showEmpty
{
    BOOL none = docs.count == 0;
    emptyLabel.hidden = !none;
    tabBar.hidden = none;
    jumpBar.hidden = none;
    split.hidden = none;
}

// ---- properties ---------------------------------------------------------------------
- (NSArray<GRSourceDocument*>*)documents { return docs; }
- (GRSourceDocument*)activeDocument { return active; }
- (BOOL)isSplit { return splitOn; }

- (GRCodeView*)activeCodeView
{
    if (secondary != nil)
    {
        GRCodeView* lower = [GRCodeView codeViewIn:secondary];
        if (lower != nil && self.window.firstResponder == lower) return lower;
    }
    NSScrollView* s = active != nil ? [primary objectForKey:active] : nil;
    return s != nil ? [GRCodeView codeViewIn:s] : nil;
}

- (NSArray<GRSourceDocument*>*)modifiedDocuments
{
    NSMutableArray* r = [NSMutableArray array];
    for (GRSourceDocument* d in docs) if (d.isModified) [r addObject:d];
    return r;
}

- (NSArray<GRCodeView*>*)codeViewsFor:(GRSourceDocument*)document
{
    NSMutableArray* r = [NSMutableArray array];
    NSScrollView* s = [primary objectForKey:document];
    if (s != nil && [GRCodeView codeViewIn:s] != nil) [r addObject:[GRCodeView codeViewIn:s]];
    if (document == active && secondary != nil && [GRCodeView codeViewIn:secondary] != nil)
        [r addObject:[GRCodeView codeViewIn:secondary]];
    return r;
}

// ---- views of a document ------------------------------------------------------------------
- (NSScrollView*)makeViewFor:(GRSourceDocument*)d
{
    NSScrollView* s = [GRCodeView scrollViewForDocument:d];
    GRCodeView* v = [GRCodeView codeViewIn:s];
    v.delegate = self;
    __weak GREditorArea* weakSelf = self;
    __weak GRSourceDocument* weakDoc = d;
    v.caretMoved = ^(NSInteger line, NSInteger column) {
        GREditorArea* me = weakSelf;
        if (me == nil) return;
        if ([me.delegate respondsToSelector:@selector(editorArea:caretAtLine:column:)])
            [me.delegate editorArea:me caretAtLine:line column:column];
        [me scheduleJumpBar];
    };
    v.breakpointToggled = ^(NSInteger line) {
        GREditorArea* me = weakSelf;
        GRSourceDocument* doc = weakDoc;
        if (me == nil || doc == nil) return;
        [me refreshGutters];
        if ([me.delegate respondsToSelector:@selector(editorArea:breakpointToggled:line:)])
            [me.delegate editorArea:me breakpointToggled:doc line:line];
    };
    return s;
}

- (void)dropView:(NSScrollView*)s
{
    if (s == nil) return;
    [[GRCodeView codeViewIn:s] detachFromDocument];
    [s removeFromSuperview];
}

// the text views share the document's undo manager (NSTextViewDelegate)
- (NSUndoManager*)undoManagerForTextView:(NSTextView*)view
{
    if ([view isKindOfClass:[GRCodeView class]])
    {
        GRSourceDocument* d = ((GRCodeView*)view).document;
        if (d != nil) return d.undoManager;
    }
    return nil;
}

// ---- opening ----------------------------------------------------------------------------
- (GRSourceDocument*)documentForPath:(NSString*)path
{
    if (path.length == 0) return nil;
    NSString* want = path.stringByStandardizingPath;
    for (GRSourceDocument* d in docs)
        if (d.path != nil && [d.path.stringByStandardizingPath caseInsensitiveCompare:want] == NSOrderedSame) return d;
    return nil;
}

- (void)addDocument:(GRSourceDocument*)d
{
    // pinned tabs stay in front: a new tab goes after the active one, or at the end
    NSUInteger at = docs.count;
    if (active != nil)
    {
        NSUInteger i = [docs indexOfObject:active];
        if (i != NSNotFound) at = i + 1;
    }
    while (at < docs.count && docs[at].pinned) at++;
    [docs insertObject:d atIndex:at];
    [[NSDocumentController sharedDocumentController] addDocument:d];
}

- (GRSourceDocument*)openPath:(NSString*)path error:(NSError**)error
{
    GRSourceDocument* d = [self documentForPath:path];
    if (d != nil) { [self activateDocument:d]; return d; }
    d = [GRSourceDocument openPath:path error:error];
    if (d == nil) return nil;
    d.projectLanguage = self.projectLanguage;
    [d detectLanguage];
    [self addDocument:d];
    [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:path]];
    [self activateDocument:d];
    [self changed];
    return d;
}

- (GRSourceDocument*)openPath:(NSString*)path line:(NSInteger)line column:(NSInteger)column error:(NSError**)error
{
    GRSourceDocument* d = [self openPath:path error:error];
    if (d != nil && line > 0) [self.activeCodeView goToLine:line column:(column > 0 ? column : 1)];
    return d;
}

- (GRSourceDocument*)newUntitled:(GRIDEm::Core::Language)language
{
    GRSourceDocument* d = [GRSourceDocument untitledWithLanguage:language];
    d.projectLanguage = self.projectLanguage;
    [self addDocument:d];
    [self activateDocument:d];
    [self changed];
    return d;
}

- (void)activateDocument:(GRSourceDocument*)d
{
    if (d == nil || [docs indexOfObject:d] == NSNotFound) return;
    if (d != active)
    {
        // the previous document's view leaves the split (it is kept), the lower pane goes
        if (active != nil)
        {
            [[primary objectForKey:active] removeFromSuperview];
            NSWindowController* wc = self.windowController;
            if (wc != nil && wc.document == active) [active removeWindowController:wc];
        }
        [self dropView:secondary];
        secondary = nil;
        active = d;
        NSScrollView* s = [primary objectForKey:d];
        if (s == nil)
        {
            s = [self makeViewFor:d];
            [primary setObject:s forKey:d];
        }
        [split addSubview:s];
        if (splitOn)
        {
            secondary = [self makeViewFor:d];
            [split addSubview:secondary];
            [split adjustSubviews];
        }
        NSWindowController* wc = self.windowController;
        if (wc != nil) [d addWindowController:wc];
    }
    [self showEmpty];
    [self refreshTabs];
    [self updateJumpBar];
    [self focusEditor];
    if ([self.delegate respondsToSelector:@selector(editorArea:didActivate:)]) [self.delegate editorArea:self didActivate:d];
    GRCodeView* v = self.activeCodeView;
    if (v != nil && [self.delegate respondsToSelector:@selector(editorArea:caretAtLine:column:)])
        [self.delegate editorArea:self caretAtLine:[v currentLine] column:[v currentColumn]];
}

- (void)selectNext:(BOOL)forward
{
    if (docs.count < 2 || active == nil) return;
    NSInteger i = (NSInteger)[docs indexOfObject:active];
    NSInteger n = (NSInteger)docs.count;
    i = forward ? (i + 1) % n : (i + n - 1) % n;
    [self activateDocument:docs[(NSUInteger)i]];
}

- (void)focusEditor
{
    GRCodeView* v = self.activeCodeView;
    if (v != nil) [self.window makeFirstResponder:v];
}

// ---- closing ------------------------------------------------------------------------------
- (void)reallyClose:(GRSourceDocument*)d
{
    NSUInteger i = [docs indexOfObject:d];
    if (i == NSNotFound) return;
    if (d == active)
    {
        [self dropView:secondary];
        secondary = nil;
    }
    [self dropView:[primary objectForKey:d]];
    [primary removeObjectForKey:d];
    NSWindowController* wc = self.windowController;
    if (wc != nil && wc.document == d) [d removeWindowController:wc];
    [docs removeObjectAtIndex:i];
    [d close];
    if (d == active)
    {
        active = nil;
        if (docs.count > 0) [self activateDocument:docs[MIN(i, docs.count - 1)]];
        else if ([self.delegate respondsToSelector:@selector(editorArea:didActivate:)]) [self.delegate editorArea:self didActivate:nil];
    }
    [self showEmpty];
    [self refreshTabs];
    [self updateJumpBar];
    [self changed];
}

- (void)document:(NSDocument*)document shouldClose:(BOOL)shouldClose contextInfo:(void*)info
{
    void (^done)(BOOL) = (__bridge_transfer void (^)(BOOL))info;
    if (shouldClose) [self reallyClose:(GRSourceDocument*)document];
    if (done) done(shouldClose);
}

- (void)closeDocument:(GRSourceDocument*)d done:(void (^)(BOOL closed))done
{
    if (d == nil) { if (done) done(YES); return; }
    if (!d.isModified)
    {
        [self reallyClose:d];
        if (done) done(YES);
        return;
    }
    // the question is a sheet on the window: the document is shown while it is asked
    [self activateDocument:d];
    void (^copy)(BOOL) = [done copy];
    [d canCloseDocumentWithDelegate:self shouldCloseSelector:@selector(document:shouldClose:contextInfo:)
                        contextInfo:(__bridge_retained void*)copy];
}

- (void)closeActive { [self closeDocument:active done:nil]; }

- (void)closeList:(NSMutableArray<GRSourceDocument*>*)list done:(void (^)(BOOL closed))done
{
    if (list.count == 0) { if (done) done(YES); return; }
    GRSourceDocument* d = list.firstObject;
    [list removeObjectAtIndex:0];
    __weak GREditorArea* weakSelf = self;
    [self closeDocument:d done:^(BOOL closed) {
        GREditorArea* me = weakSelf;
        if (!closed || me == nil) { if (done) done(NO); return; }
        [me closeList:list done:done];
    }];
}

- (void)closeAll:(void (^)(BOOL closed))done
{
    [self closeList:[docs mutableCopy] done:done];
}

- (void)closeAllBut:(GRSourceDocument*)keep done:(void (^)(BOOL closed))done
{
    NSMutableArray* list = [docs mutableCopy];
    [list removeObject:keep];
    [self closeList:list done:done];
}

- (void)discardAll
{
    for (GRSourceDocument* d in [docs copy]) [self reallyClose:d];
}

// ---- saving ----------------------------------------------------------------------------------
- (BOOL)saveAll:(NSError**)error
{
    BOOL ok = YES;
    for (GRSourceDocument* d in docs)
    {
        if (!d.isModified || d.path == nil || d.readOnly) continue;
        if (![d saveNow:error]) ok = NO;
    }
    [self refreshTabs];
    return ok;
}

- (void)saveDocument:(GRSourceDocument*)d
{
    if (d == nil || d.readOnly) return;
    if (d.path != nil)
    {
        NSError* err = nil;
        if (![d saveNow:&err] && err != nil) [self presentError:err];
        return;
    }
    [self activateDocument:d];
    [d saveDocumentWithDelegate:nil didSaveSelector:nullptr contextInfo:nullptr];
}

// ---- tabs ----------------------------------------------------------------------------------------
- (void)refreshTabs
{
    NSMutableArray<GRTabItem*>* items = [NSMutableArray array];
    for (GRSourceDocument* d in docs)
    {
        GRTabItem* it = [[GRTabItem alloc] init];
        it.title = d.displayName;
        it.path = d.path;
        it.modified = d.isModified;
        it.pinned = d.pinned;
        it.readOnly = d.readOnly;
        it.representedObject = d;
        [items addObject:it];
    }
    NSInteger sel = active != nil ? (NSInteger)[docs indexOfObject:active] : -1;
    [tabBar setItems:items selected:sel];
}

- (void)changed
{
    if ([self.delegate respondsToSelector:@selector(editorAreaDocumentsChanged:)]) [self.delegate editorAreaDocumentsChanged:self];
}

- (void)documentState:(NSNotification*)n
{
    if ([docs indexOfObject:n.object] == NSNotFound) return;
    GRSourceDocument* d = n.object;
    for (GRCodeView* v in [self codeViewsFor:d])
    {
        v.editable = !d.readOnly;
        [v applyLanguage];
    }
    [self refreshTabs];
    [self updateJumpBar];
    [self changed];
}

- (void)tabBar:(GRTabBar*)bar didSelect:(NSInteger)index
{
    (void)bar;
    if (index >= 0 && index < (NSInteger)docs.count) [self activateDocument:docs[(NSUInteger)index]];
}

- (void)tabBar:(GRTabBar*)bar close:(NSInteger)index
{
    (void)bar;
    if (index >= 0 && index < (NSInteger)docs.count) [self closeDocument:docs[(NSUInteger)index] done:nil];
}

- (void)tabBar:(GRTabBar*)bar save:(NSInteger)index
{
    (void)bar;
    if (index >= 0 && index < (NSInteger)docs.count) [self saveDocument:docs[(NSUInteger)index]];
}

- (void)tabBar:(GRTabBar*)bar closeOthers:(NSInteger)index
{
    (void)bar;
    if (index >= 0 && index < (NSInteger)docs.count) [self closeAllBut:docs[(NSUInteger)index] done:nil];
}

- (void)tabBarCloseAll:(GRTabBar*)bar { (void)bar; [self closeAll:nil]; }

- (void)tabBar:(GRTabBar*)bar togglePin:(NSInteger)index
{
    (void)bar;
    if (index >= 0 && index < (NSInteger)docs.count) [self togglePin:docs[(NSUInteger)index]];
}

- (void)tabBar:(GRTabBar*)bar moveFrom:(NSInteger)from to:(NSInteger)to
{
    (void)bar;
    if (from < 0 || to < 0 || from >= (NSInteger)docs.count || to >= (NSInteger)docs.count) return;
    GRSourceDocument* d = docs[(NSUInteger)from];
    [docs removeObjectAtIndex:(NSUInteger)from];
    [docs insertObject:d atIndex:(NSUInteger)to];
}

// a pinned tab moves to the end of the pinned ones; unpinned, to the first unpinned place
- (void)togglePin:(GRSourceDocument*)d
{
    if (d == nil) return;
    [docs removeObject:d];
    d.pinned = !d.pinned;
    NSUInteger at = 0;
    while (at < docs.count && docs[at].pinned) at++;
    [docs insertObject:d atIndex:at];
    [self refreshTabs];
}

// ---- the jump bar -------------------------------------------------------------------------------
- (void)scheduleJumpBar
{
    if (jumpScheduled) return;
    jumpScheduled = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(150 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        self->jumpScheduled = NO;
        [self updateJumpBar];
    });
}

// the function the caret is in: the name before the "(" of the last top-level "{" opened before it
static NSString* FunctionAt(NSString* text, NSUInteger caret)
{
    NSUInteger n = MIN(caret, text.length);
    int depth = 0;
    NSString* current = nil;
    NSUInteger i = 0;
    while (i < n)
    {
        unichar c = [text characterAtIndex:i];
        if (c == '/' && i + 1 < n && [text characterAtIndex:i + 1] == '/')
        {
            while (i < n && [text characterAtIndex:i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < n && [text characterAtIndex:i + 1] == '*')
        {
            NSRange e = [text rangeOfString:@"*/" options:0 range:NSMakeRange(i + 2, text.length - i - 2)];
            i = e.location == NSNotFound ? n : NSMaxRange(e);
            continue;
        }
        if (c == '"' || c == '\'')
        {
            NSUInteger j = i + 1;
            while (j < n && [text characterAtIndex:j] != c && [text characterAtIndex:j] != '\n')
            {
                if ([text characterAtIndex:j] == '\\') j++;
                j++;
            }
            i = j + 1;
            continue;
        }
        if (c == '{')
        {
            if (depth == 0)
            {
                // back over ") const {" to the "(", then the identifier before it
                NSInteger k = (NSInteger)i - 1;
                NSInteger stop = MAX(0, (NSInteger)i - 400);
                while (k >= stop && [text characterAtIndex:(NSUInteger)k] != ')' && [text characterAtIndex:(NSUInteger)k] != ';'
                       && [text characterAtIndex:(NSUInteger)k] != '}')
                    k--;
                current = nil;
                if (k >= stop && [text characterAtIndex:(NSUInteger)k] == ')')
                {
                    int paren = 0;
                    for (; k >= stop; k--)
                    {
                        unichar x = [text characterAtIndex:(NSUInteger)k];
                        if (x == ')') paren++;
                        else if (x == '(' && --paren == 0) break;
                    }
                    NSInteger e = k - 1;
                    while (e >= stop && ([text characterAtIndex:(NSUInteger)e] == ' ' || [text characterAtIndex:(NSUInteger)e] == '\t')) e--;
                    NSInteger b = e;
                    while (b >= stop)
                    {
                        unichar x = [text characterAtIndex:(NSUInteger)b];
                        if ((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || (x >= '0' && x <= '9') || x == '_' || x == ':' || x == '~')
                            b--;
                        else break;
                    }
                    if (e > b) current = [[text substringWithRange:NSMakeRange((NSUInteger)(b + 1), (NSUInteger)(e - b))]
                                             stringByAppendingString:@"()"];
                }
            }
            depth++;
        }
        else if (c == '}')
        {
            depth = MAX(0, depth - 1);
            if (depth == 0) current = nil;
        }
        i++;
    }
    return current;
}

- (NSPathControlItem*)jumpItem:(NSString*)title image:(NSImage*)image path:(NSString*)path
{
    NSPathControlItem* it = [[NSPathControlItem alloc] init];
    it.title = GROr(title, @"");
    it.image = image;
    [jumpPaths addObject:(path != nil ? (id)path : (id)[NSNull null])];
    return it;
}

- (void)updateJumpBar
{
    GRSourceDocument* d = active;
    jumpPaths = [NSMutableArray array];
    if (d == nil) { jumpBar.pathItems = @[]; return; }
    NSMutableArray<NSPathControlItem*>* items = [NSMutableArray array];
    NSString* path = d.path;
    NSString* root = self.projectRoot.stringByStandardizingPath;
    if (path != nil && root.length > 0 && [path.stringByStandardizingPath hasPrefix:[root stringByAppendingString:@"/"]])
    {
        [items addObject:[self jumpItem:(self.projectName.length > 0 ? self.projectName : root.lastPathComponent)
                                  image:[GRIcons image:@"project" size:11] path:root]];
        NSString* rel = [path.stringByStandardizingPath substringFromIndex:root.length + 1];
        NSArray<NSString*>* parts = rel.pathComponents;
        NSString* acc = root;
        for (NSUInteger i = 0; i + 1 < parts.count; i++)
        {
            acc = [acc stringByAppendingPathComponent:parts[i]];
            [items addObject:[self jumpItem:parts[i] image:[GRIcons image:@"folder" size:11] path:acc]];
        }
    }
    else if (path != nil)
    {
        NSString* dir = path.stringByDeletingLastPathComponent;
        [items addObject:[self jumpItem:dir.lastPathComponent image:[GRIcons image:@"folder" size:11] path:dir]];
    }
    [items addObject:[self jumpItem:d.displayName image:[GRIcons imageForFile:GROr(path, d.displayName)] path:nil]];
    GRCodeView* v = self.activeCodeView;
    if (v != nil)
    {
        NSString* fn = FunctionAt(v.string, v.selectedRange.location);
        if (fn != nil) [items addObject:[self jumpItem:fn image:[NSImage imageWithSystemSymbolName:@"f.cursive"
                                                                              accessibilityDescription:@"function"] path:nil]];
    }
    jumpBar.pathItems = items;
}

- (void)jumpBarClicked:(id)sender
{
    (void)sender;
    NSPathControlItem* it = jumpBar.clickedPathItem;
    if (it == nil) return;
    NSArray<NSPathControlItem*>* items = jumpBar.pathItems;
    NSUInteger i = [items indexOfObjectIdenticalTo:it];
    if (i == NSNotFound)
        for (NSUInteger k = 0; k < items.count; k++)
            if ([items[k].title isEqualToString:it.title]) { i = k; break; }
    if (i == NSNotFound || i >= jumpPaths.count || jumpPaths[i] == [NSNull null]) return;
    if ([self.delegate respondsToSelector:@selector(editorArea:jumpToPath:)])
        [self.delegate editorArea:self jumpToPath:jumpPaths[i]];
}

// ---- views ---------------------------------------------------------------------------------------
- (void)toggleSplit
{
    if (active == nil) return;
    splitOn = !splitOn;
    if (splitOn)
    {
        secondary = [self makeViewFor:active];
        [split addSubview:secondary];
        [split adjustSubviews];
        CGFloat h = split.bounds.size.height;
        if (h > 120) [split setPosition:MAX(60, h / 3) ofDividerAtIndex:0];
    }
    else
    {
        [self dropView:secondary];
        secondary = nil;
        [split adjustSubviews];
    }
    [self focusEditor];
}

- (void)applySettings
{
    GRIDEm::Settings::GrideSettingsPtr s = [GRSourceDocument settings];
    NSMutableArray<GRCodeView*>* views = [NSMutableArray array];
    for (GRSourceDocument* d in docs)
    {
        NSScrollView* sv = [primary objectForKey:d];
        if (sv != nil && [GRCodeView codeViewIn:sv] != nil) [views addObject:[GRCodeView codeViewIn:sv]];
    }
    if (secondary != nil && [GRCodeView codeViewIn:secondary] != nil) [views addObject:[GRCodeView codeViewIn:secondary]];
    for (GRCodeView* v in views)
    {
        if (s) [v applyEnvironment:s->Env];
        GRTheme* t = [[GRTheme current] resolvedFor:v.effectiveAppearance];
        if (s) [t applyOverrides:s->Env->SyntaxColors];
        [v applyTheme:t];
        [v applyLanguage];
    }
    [tabBar reload];
}

- (void)showExecutionLine:(NSInteger)line inPath:(NSString*)path
{
    GRSourceDocument* target = line > 0 ? [self documentForPath:path] : nil;
    for (GRSourceDocument* d in docs)
        for (GRCodeView* v in [self codeViewsFor:d])
            if (d != target && v.executionLine != 0) v.executionLine = 0;
    if (target == nil) return;
    [self activateDocument:target];
    for (GRCodeView* v in [self codeViewsFor:target]) v.executionLine = line;
}

- (void)refreshGutters
{
    for (GRSourceDocument* d in docs)
        for (GRCodeView* v in [self codeViewsFor:d]) [v.gutter setNeedsDisplay:YES];
}
@end
