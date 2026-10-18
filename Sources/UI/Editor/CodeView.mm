// =============================================================================
//  CodeView.mm - see CodeView.h
// =============================================================================
#import "CodeView.h"
#import "../Common.h"
#import "Gutter.h"
#import "Highlighter.h"
#import "SourceDocument.h"
#include <algorithm>
#include <vector>

@interface GRCodeView ()
@property (nonatomic, weak, readwrite) GRSourceDocument* document;
@property (nonatomic, readwrite) GRHighlighter* highlighter;
@property (nonatomic, readwrite) GRGutter* gutter;
@end

@implementation GRCodeView
{
    NSRange braceA, braceB;          // the braces marked, NSNotFound when none
    NSNotificationCenter* center;
}

// ---- making one ---------------------------------------------------------------------
+ (NSScrollView*)scrollViewForDocument:(GRSourceDocument*)document
{
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 600, 400)];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSLayoutManager* lm = [[NSLayoutManager alloc] init];
    lm.allowsNonContiguousLayout = YES;
    [document.textStorage addLayoutManager:lm];
    NSTextContainer* tc = [[NSTextContainer alloc] initWithSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
    tc.widthTracksTextView = NO;                     // code does not wrap
    tc.lineFragmentPadding = 4;
    [lm addTextContainer:tc];

    NSSize content = scroll.contentSize;
    GRCodeView* v = [[GRCodeView alloc] initWithFrame:NSMakeRect(0, 0, content.width, content.height) textContainer:tc];
    v.document = document;
    v.minSize = NSMakeSize(0, content.height);
    v.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    v.verticallyResizable = YES;
    v.horizontallyResizable = YES;
    v.autoresizingMask = NSViewWidthSizable;
    scroll.documentView = v;

    GRGutter* g = [[GRGutter alloc] initWithCodeView:v];
    scroll.verticalRulerView = g;
    scroll.hasVerticalRuler = YES;
    scroll.rulersVisible = YES;
    v.gutter = g;

    [v setUp];
    return scroll;
}

+ (GRCodeView*)codeViewIn:(NSScrollView*)scrollView
{
    id v = scrollView.documentView;
    return [v isKindOfClass:[GRCodeView class]] ? (GRCodeView*)v : nil;
}

- (void)setUp
{
    braceA = NSMakeRange(NSNotFound, 0);
    braceB = NSMakeRange(NSNotFound, 0);
    _tabSize = 4;
    _insertSpaces = NO;
    _autoIndent = YES;
    _fontName = @"Menlo";
    _fontSize = 12;
    self.richText = NO;
    self.importsGraphics = NO;
    self.allowsUndo = YES;
    self.usesFindBar = YES;
    self.incrementalSearchingEnabled = YES;
    self.automaticQuoteSubstitutionEnabled = NO;
    self.automaticDashSubstitutionEnabled = NO;
    self.automaticTextReplacementEnabled = NO;
    self.automaticSpellingCorrectionEnabled = NO;
    self.continuousSpellCheckingEnabled = NO;
    self.grammarCheckingEnabled = NO;
    self.smartInsertDeleteEnabled = NO;
    self.automaticLinkDetectionEnabled = NO;
    self.automaticDataDetectionEnabled = NO;
    self.displaysLinkToolTips = NO;
    self.usesFontPanel = NO;
    self.usesRuler = NO;
    self.editable = !self.document.readOnly;

    self.highlighter = [[GRHighlighter alloc] initWithLanguage:nullptr];
    GRIDEm::Settings::GrideSettingsPtr s = [GRSourceDocument settings];
    if (s) [self applyEnvironment:s->Env];
    GRTheme* t = [[GRTheme current] resolvedFor:[NSApp effectiveAppearance]];
    if (s) [t applyOverrides:s->Env->SyntaxColors];
    [self applyTheme:t];
    [self applyLanguage];
    [self.highlighter attachToTextView:self];

    center = [NSNotificationCenter defaultCenter];
    [center addObserver:self selector:@selector(textDidChangeHere:) name:NSTextDidChangeNotification object:self];
    [center addObserver:self selector:@selector(themeChanged:) name:GRThemeDidChangeNotification object:nil];
}

- (void)detachFromDocument
{
    [center removeObserver:self];
    [self.highlighter detach];
    GRSourceDocument* d = self.document;
    NSLayoutManager* lm = self.layoutManager;
    if (d != nil && lm != nil) [d.textStorage removeLayoutManager:lm];
    self.document = nil;
}

- (void)dealloc { [[NSNotificationCenter defaultCenter] removeObserver:self]; }

// ---- settings -------------------------------------------------------------------------
- (NSFont*)codeFont
{
    NSFont* f = [NSFont fontWithName:self.fontName size:self.fontSize];
    if (f == nil) f = [NSFont monospacedSystemFontOfSize:self.fontSize weight:NSFontWeightRegular];
    return f;
}

- (void)applyEnvironment:(GRIDEm::Settings::EnvironmentSettingsPtr)env
{
    if (!env) return;
    self.tabSize = env->TabSize > 0 ? env->TabSize : 4;
    self.insertSpaces = env->InsertSpaces;
    self.fontName = GROr([NSString stringWithUTF8String:env->FontName.c_str()], @"Menlo");
    self.fontSize = env->FontSize > 0 ? env->FontSize : 12;
    self.showWhitespace = env->ShowWhitespace;
    self.gutter.showNumbers = env->LineNumbers;
    [self applyFont];
}

- (void)applyFont
{
    NSFont* f = [self codeFont];
    self.font = f;
    // tab stops every tabSize spaces
    NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
    CGFloat space = [@" " sizeWithAttributes:@{ NSFontAttributeName : f }].width;
    ps.tabStops = @[];
    ps.defaultTabInterval = space * self.tabSize;
    NSMutableDictionary* typing = GROr([self.typingAttributes mutableCopy], [NSMutableDictionary dictionary]);
    typing[NSFontAttributeName] = f;
    typing[NSParagraphStyleAttributeName] = ps;
    if (self.theme != nil) typing[NSForegroundColorAttributeName] = self.theme.editorText;
    self.typingAttributes = typing;
    self.defaultParagraphStyle = ps;
    NSTextStorage* ts = self.textStorage;
    if (ts.length > 0)
    {
        [ts beginEditing];
        [ts addAttribute:NSFontAttributeName value:f range:NSMakeRange(0, ts.length)];
        [ts addAttribute:NSParagraphStyleAttributeName value:ps range:NSMakeRange(0, ts.length)];
        [ts endEditing];
    }
    [self.gutter updateWidth];
    [self.highlighter highlightVisible];
}

- (void)setShowWhitespace:(BOOL)on
{
    _showWhitespace = on;
    self.layoutManager.showsInvisibleCharacters = on;
    [self setNeedsDisplay:YES];
}

- (void)applyTheme:(GRTheme*)theme
{
    self.theme = theme;
    self.backgroundColor = theme.window;
    self.insertionPointColor = theme.editorText;
    self.selectedTextAttributes = @{ NSBackgroundColorAttributeName : theme.selection };
    NSMutableDictionary* typing = GROr([self.typingAttributes mutableCopy], [NSMutableDictionary dictionary]);
    typing[NSForegroundColorAttributeName] = theme.editorText;
    self.typingAttributes = typing;
    NSTextStorage* ts = self.textStorage;
    if (ts.length > 0) [ts addAttribute:NSForegroundColorAttributeName value:theme.editorText range:NSMakeRange(0, ts.length)];
    self.highlighter.theme = theme;
    [self.highlighter highlightVisible];
    [self.gutter setNeedsDisplay:YES];
    [self setNeedsDisplay:YES];
}

- (void)themeChanged:(NSNotification*)n
{
    (void)n;
    GRTheme* t = [[GRTheme current] resolvedFor:self.effectiveAppearance];
    GRIDEm::Settings::GrideSettingsPtr s = [GRSourceDocument settings];
    if (s) [t applyOverrides:s->Env->SyntaxColors];
    [self applyTheme:t];
}

// the System theme follows the Mac's mode as it changes
- (void)viewDidChangeEffectiveAppearance
{
    [super viewDidChangeEffectiveAppearance];
    if ([GRTheme current].followsSystem) [self themeChanged:nil];
}

- (void)applyLanguage
{
    GRSourceDocument* d = self.document;
    GRIDEm::Settings::GrideSettingsPtr s = [GRSourceDocument settings];
    if (d == nil || !s) { [self.highlighter setLanguage:nullptr]; return; }
    [self.highlighter setLanguage:s->LanguageOf(d.language)];
}

- (void)zoomBy:(CGFloat)delta
{
    self.fontSize = std::max<CGFloat>(6, std::min<CGFloat>(72, self.fontSize + delta));
    [self applyFont];
}

- (void)resetZoom:(CGFloat)size
{
    self.fontSize = size > 0 ? size : 12;
    [self applyFont];
}

// ---- lines --------------------------------------------------------------------------
- (NSInteger)lineOfCharacter:(NSUInteger)index
{
    NSString* s = self.string;
    NSUInteger n = MIN(index, s.length);
    NSInteger line = 1;
    for (NSUInteger i = 0; i < n; i++)
        if ([s characterAtIndex:i] == '\n') line++;
    return line;
}

- (NSInteger)currentLine { return [self lineOfCharacter:self.selectedRange.location]; }

- (NSInteger)currentColumn
{
    NSString* s = self.string;
    NSUInteger loc = MIN(self.selectedRange.location, s.length);
    NSRange line = [s lineRangeForRange:NSMakeRange(loc, 0)];
    return (NSInteger)(loc - line.location) + 1;
}

- (NSInteger)lineCount { return [self lineOfCharacter:self.string.length]; }

// the start of a 1-based line, or the end of the text when there are fewer
- (NSUInteger)startOfLine:(NSInteger)line
{
    NSString* s = self.string;
    if (line <= 1) return 0;
    NSInteger l = 1;
    for (NSUInteger i = 0; i < s.length; i++)
        if ([s characterAtIndex:i] == '\n' && ++l == line) return i + 1;
    return s.length;
}

- (NSRange)rangeOfLine:(NSInteger)line
{
    NSString* s = self.string;
    NSUInteger start = [self startOfLine:line];
    NSUInteger end = start;
    while (end < s.length && [s characterAtIndex:end] != '\n') end++;
    return NSMakeRange(start, end - start);
}

- (void)goToLine:(NSInteger)line
{
    if (line < 1) line = 1;
    NSUInteger at = [self startOfLine:line];
    self.selectedRange = NSMakeRange(at, 0);
    [self scrollRangeToVisible:[self.string lineRangeForRange:NSMakeRange(at, 0)]];
    [self.window makeFirstResponder:self];
}

- (void)goToLine:(NSInteger)line column:(NSInteger)column
{
    NSRange r = [self rangeOfLine:line < 1 ? 1 : line];
    NSUInteger offset = (NSUInteger)std::max<NSInteger>(0, std::min<NSInteger>(column - 1, (NSInteger)r.length));
    self.selectedRange = NSMakeRange(r.location + offset, 0);
    [self scrollRangeToVisible:self.selectedRange];
    [self.window makeFirstResponder:self];
}

- (void)selectRange:(NSRange)range
{
    if (NSMaxRange(range) > self.string.length) return;
    self.selectedRange = range;
    [self scrollRangeToVisible:range];
}

// the first and last line the selection touches (a selection ending at a line start leaves that line out)
- (void)selectedFirst:(NSInteger*)first last:(NSInteger*)last
{
    NSRange sel = self.selectedRange;
    *first = [self lineOfCharacter:sel.location];
    NSUInteger end = NSMaxRange(sel);
    if (sel.length > 0 && end > 0 && [self.string characterAtIndex:end - 1] == '\n') end--;
    *last = [self lineOfCharacter:end];
}

- (NSRange)rangeOfLines:(NSInteger)first to:(NSInteger)last
{
    NSRange a = [self rangeOfLine:first];
    NSRange b = [self rangeOfLine:last];
    return NSMakeRange(a.location, NSMaxRange(b) - a.location);
}

- (NSMutableArray<NSString*>*)linesFrom:(NSInteger)first to:(NSInteger)last
{
    NSString* block = [self.string substringWithRange:[self rangeOfLines:first to:last]];
    return [[block componentsSeparatedByString:@"\n"] mutableCopy];
}

// replaces whole lines as one undo step and selects them
- (void)replaceText:(NSRange)range with:(NSString*)text selectAll:(BOOL)select
{
    if (!self.editable) return;
    if (![self shouldChangeTextInRange:range replacementString:text]) return;
    [self.textStorage replaceCharactersInRange:range withString:text];
    [self didChangeText];
    if (select) self.selectedRange = NSMakeRange(range.location, text.length);
}

- (void)replaceLines:(NSInteger)first to:(NSInteger)last with:(NSArray<NSString*>*)lines
{
    [self replaceText:[self rangeOfLines:first to:last] with:[lines componentsJoinedByString:@"\n"] selectAll:YES];
}

- (NSString*)indentUnit
{
    if (!self.insertSpaces) return @"\t";
    return [@"" stringByPaddingToLength:(NSUInteger)self.tabSize withString:@" " startingAtIndex:0];
}

static NSString* TrimStart(NSString* s)
{
    NSUInteger i = 0;
    while (i < s.length && ([s characterAtIndex:i] == ' ' || [s characterAtIndex:i] == '\t')) i++;
    return [s substringFromIndex:i];
}

static NSString* Trim(NSString* s)
{
    return [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

// ---- editing --------------------------------------------------------------------------
- (void)commentSelection:(BOOL)comment
{
    GRSourceDocument* d = self.document;
    GRIDEm::Settings::GrideSettingsPtr s = [GRSourceDocument settings];
    if (d == nil || !s) return;
    GRIDEm::Settings::LanguageSettingsPtr ls = s->LanguageOf(d.language);
    if (!ls) return;
    NSString* commentLine = [NSString stringWithUTF8String:ls->LineComment.c_str()];
    NSString* commentStart = [NSString stringWithUTF8String:ls->BlockCommentStart.c_str()];
    NSString* commentEnd = [NSString stringWithUTF8String:ls->BlockCommentEnd.c_str()];
    BOOL useLine = commentLine.length > 0;
    if (!useLine && commentStart.length == 0) return;
    NSInteger first, last;
    [self selectedFirst:&first last:&last];
    NSMutableArray<NSString*>* lines = [self linesFrom:first to:last];
    NSUInteger minIndent = NSUIntegerMax;
    for (NSString* l in lines)
        if (Trim(l).length > 0) minIndent = MIN(minIndent, l.length - TrimStart(l).length);
    if (minIndent == NSUIntegerMax) minIndent = 0;
    for (NSUInteger i = 0; i < lines.count; i++)
    {
        NSString* l = lines[i];
        if (Trim(l).length == 0) continue;
        if (comment)
        {
            NSString* head = [l substringToIndex:minIndent];
            NSString* body = [l substringFromIndex:minIndent];
            if (useLine) lines[i] = [NSString stringWithFormat:@"%@%@ %@", head, commentLine, body];
            else lines[i] = [NSString stringWithFormat:@"%@%@ %@ %@", head, commentStart, body, commentEnd];
        }
        else
        {
            NSString* body = TrimStart(l);
            NSString* ind = [l substringToIndex:l.length - body.length];
            NSString* bodyEnd = [body stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            if (useLine && [body hasPrefix:commentLine])
            {
                body = [body substringFromIndex:commentLine.length];
                if ([body hasPrefix:@" "]) body = [body substringFromIndex:1];
            }
            else if (commentStart.length > 0 && [body hasPrefix:commentStart] && [bodyEnd hasSuffix:commentEnd]
                     && bodyEnd.length >= commentStart.length + commentEnd.length)
            {
                body = [bodyEnd substringWithRange:NSMakeRange(commentStart.length,
                                                               bodyEnd.length - commentStart.length - commentEnd.length)];
                if ([body hasPrefix:@" "]) body = [body substringFromIndex:1];
                if ([body hasSuffix:@" "]) body = [body substringToIndex:body.length - 1];
            }
            lines[i] = [ind stringByAppendingString:body];
        }
    }
    [self replaceLines:first to:last with:lines];
}

- (void)indentSelection:(BOOL)indent
{
    NSInteger first, last;
    [self selectedFirst:&first last:&last];
    NSMutableArray<NSString*>* lines = [self linesFrom:first to:last];
    NSString* unit = [self indentUnit];
    for (NSUInteger i = 0; i < lines.count; i++)
    {
        NSString* l = lines[i];
        if (indent) { if (l.length > 0) lines[i] = [unit stringByAppendingString:l]; }
        else if ([l hasPrefix:@"\t"]) lines[i] = [l substringFromIndex:1];
        else
        {
            NSUInteger n = 0;
            while ((NSInteger)n < self.tabSize && n < l.length && [l characterAtIndex:n] == ' ') n++;
            lines[i] = [l substringFromIndex:n];
        }
    }
    [self replaceLines:first to:last with:lines];
}

- (void)changeCase:(BOOL)upper
{
    NSRange sel = self.selectedRange;
    if (sel.length == 0 || !self.editable) return;
    NSString* t = [self.string substringWithRange:sel];
    // invariant case of ASCII and the rest, as ToUpperInvariant / ToLowerInvariant
    NSLocale* posix = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    NSString* changed = upper ? [t uppercaseStringWithLocale:posix] : [t lowercaseStringWithLocale:posix];
    [self replaceText:sel with:changed selectAll:YES];
}

- (void)convertIndentation:(BOOL)toTabs
{
    NSMutableArray<NSString*>* lines = [[self.string componentsSeparatedByString:@"\n"] mutableCopy];
    int ts = self.tabSize > 0 ? self.tabSize : 4;
    for (NSUInteger i = 0; i < lines.count; i++)
    {
        NSString* l = lines[i];
        NSUInteger n = 0;
        int col = 0;
        while (n < l.length && ([l characterAtIndex:n] == ' ' || [l characterAtIndex:n] == '\t'))
        {
            col = [l characterAtIndex:n] == '\t' ? (col / ts + 1) * ts : col + 1;
            n++;
        }
        NSString* lead = toTabs
            ? [[@"" stringByPaddingToLength:(NSUInteger)(col / ts) withString:@"\t" startingAtIndex:0]
                  stringByAppendingString:[@"" stringByPaddingToLength:(NSUInteger)(col % ts) withString:@" " startingAtIndex:0]]
            : [@"" stringByPaddingToLength:(NSUInteger)col withString:@" " startingAtIndex:0];
        lines[i] = [lead stringByAppendingString:[l substringFromIndex:n]];
    }
    NSRange keep = self.selectedRange;
    [self replaceText:NSMakeRange(0, self.string.length) with:[lines componentsJoinedByString:@"\n"] selectAll:NO];
    if (NSMaxRange(keep) <= self.string.length) self.selectedRange = keep;
}

- (void)tabify { [self convertIndentation:!self.insertSpaces]; }

static int CountBraces(NSString* body)
{
    int depth = 0;
    BOOL inString = NO;
    unichar quote = 0;
    for (NSUInteger i = 0; i < body.length; i++)
    {
        unichar c = [body characterAtIndex:i];
        if (inString)
        {
            if (c == '\\') { i++; continue; }
            if (c == quote) inString = NO;
            continue;
        }
        if (c == '"' || c == '\'') { inString = YES; quote = c; continue; }
        if (c == '/' && i + 1 < body.length && [body characterAtIndex:i + 1] == '/') break;
        if (c == '{') depth++;
        else if (c == '}') depth--;
    }
    return depth;
}

// re-indents the document by brace depth ( { } ), preprocessor lines at column 0
- (void)formatDocument
{
    NSMutableArray<NSString*>* lines = [[self.string componentsSeparatedByString:@"\n"] mutableCopy];
    NSString* unit = [self indentUnit];
    int depth = 0;
    BOOL inComment = NO;
    for (NSUInteger i = 0; i < lines.count; i++)
    {
        NSString* body = Trim(lines[i]);
        if (inComment)
        {
            if ([body rangeOfString:@"*/"].location != NSNotFound) inComment = NO;
            continue;                   // comment blocks stay as they are
        }
        if ([body hasPrefix:@"/*"] && [body rangeOfString:@"*/"].location == NSNotFound) inComment = YES;
        if (body.length == 0) { lines[i] = @""; continue; }
        if ([body hasPrefix:@"#"]) { lines[i] = body; continue; }
        int d = depth;
        if ([body hasPrefix:@"}"]) d = std::max(0, d - 1);
        NSMutableString* sb = [NSMutableString string];
        for (int k = 0; k < d; k++) [sb appendString:unit];
        [sb appendString:body];
        lines[i] = sb;
        depth = std::max(0, depth + CountBraces(body));
    }
    NSRange keep = self.selectedRange;
    [self replaceText:NSMakeRange(0, self.string.length) with:[lines componentsJoinedByString:@"\n"] selectAll:NO];
    self.selectedRange = NSMakeRange(MIN(keep.location, self.string.length), 0);
}

- (void)insertSnippet:(NSString*)text
{
    if (!self.editable) return;
    NSString* t = [text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"];
    [self insertText:t replacementRange:self.selectedRange];
    [self.window makeFirstResponder:self];
}

// ---- typing ---------------------------------------------------------------------------
- (void)insertNewline:(id)sender
{
    if (!self.autoIndent) { [super insertNewline:sender]; return; }
    NSString* s = self.string;
    NSUInteger caret = self.selectedRange.location;
    NSRange line = [s lineRangeForRange:NSMakeRange(caret, 0)];
    NSString* before = [s substringWithRange:NSMakeRange(line.location, caret - line.location)];
    NSString* lineText = [s substringWithRange:line];
    NSUInteger n = 0;
    while (n < lineText.length && ([lineText characterAtIndex:n] == ' ' || [lineText characterAtIndex:n] == '\t')) n++;
    NSString* indent = [lineText substringToIndex:MIN(n, before.length)];
    NSString* trimmed = [before stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([trimmed hasSuffix:@"{"]) indent = [indent stringByAppendingString:[self indentUnit]];
    [self insertText:[@"\n" stringByAppendingString:indent] replacementRange:self.selectedRange];
}

- (void)insertTab:(id)sender
{
    NSRange sel = self.selectedRange;
    BOOL multiLine = sel.length > 0 && [self lineOfCharacter:sel.location] != [self lineOfCharacter:NSMaxRange(sel)];
    if (multiLine) { [self indentSelection:YES]; return; }
    if (self.insertSpaces)
    {
        NSInteger col = [self currentColumn] - 1;
        NSUInteger count = (NSUInteger)(self.tabSize - (col % self.tabSize));
        [self insertText:[@"" stringByPaddingToLength:count withString:@" " startingAtIndex:0] replacementRange:sel];
        return;
    }
    [super insertTab:sender];
}

- (void)insertBacktab:(id)sender
{
    (void)sender;
    [self indentSelection:NO];
}

// ---- bookmarks and breakpoints ------------------------------------------------------------
- (void)toggleBookmark
{
    GRSourceDocument* d = self.document;
    if (d == nil) return;
    std::set<int> b = d.bookmarks;
    int l = (int)[self currentLine];
    if (b.count(l)) b.erase(l); else b.insert(l);
    d.bookmarks = b;
    [self.gutter setNeedsDisplay:YES];
}

- (void)nextBookmark:(BOOL)forward
{
    GRSourceDocument* d = self.document;
    if (d == nil) return;
    std::set<int> b = d.bookmarks;
    if (b.empty()) return;
    int cur = (int)[self currentLine];
    int target = -1;
    if (forward)
    {
        for (int x : b) if (x > cur) { target = x; break; }
        if (target < 0) target = *b.begin();
    }
    else
    {
        for (int x : b) if (x < cur) target = x;
        if (target < 0) target = *b.rbegin();
    }
    [self goToLine:target];
}

- (void)clearBookmarks
{
    GRSourceDocument* d = self.document;
    if (d == nil) return;
    d.bookmarks = std::set<int>();
    [self.gutter setNeedsDisplay:YES];
}

- (void)toggleBreakpoint:(NSInteger)line
{
    GRSourceDocument* d = self.document;
    if (d == nil) return;
    if (line <= 0) line = [self currentLine];
    std::shared_ptr<std::set<int> > bp = d.breakpoints;
    if (!bp) { bp = std::make_shared<std::set<int> >(); d.breakpoints = bp; }
    int l = (int)line;
    if (bp->count(l)) bp->erase(l); else bp->insert(l);
    [self.gutter setNeedsDisplay:YES];
    if (self.breakpointToggled) self.breakpointToggled(line);
}

// ---- the execution line and the current line ------------------------------------------------
- (void)setExecutionLine:(NSInteger)line
{
    _executionLine = line;
    if (line > 0) [self goToLine:line];
    [self.gutter setNeedsDisplay:YES];
    [self setNeedsDisplay:YES];
}

- (NSRect)rectOfLine:(NSInteger)line
{
    NSLayoutManager* lm = self.layoutManager;
    NSRange r = [self rangeOfLine:line];
    NSRect rect;
    if (r.location >= self.string.length && self.string.length > 0 && line > [self lineCount])
        return NSZeroRect;
    if (self.string.length == 0 || r.location >= self.string.length)
        rect = lm.extraLineFragmentRect;
    else
    {
        NSUInteger glyph = [lm glyphIndexForCharacterAtIndex:r.location];
        rect = [lm lineFragmentRectForGlyphAtIndex:glyph effectiveRange:nullptr];
    }
    rect.origin.x = 0;
    rect.size.width = MAX(self.bounds.size.width, self.enclosingScrollView.contentSize.width);
    rect.origin.y += self.textContainerOrigin.y;
    return rect;
}

- (void)drawViewBackgroundInRect:(NSRect)rect
{
    [super drawViewBackgroundInRect:rect];
    if (self.theme == nil) return;
    if (self.selectedRange.length == 0 && self.window.firstResponder == self)
    {
        NSRect r = [self rectOfLine:[self currentLine]];
        if (!NSIsEmptyRect(r) && NSIntersectsRect(r, rect)) { [self.theme.currentLine set]; NSRectFill(NSIntersectionRect(r, rect)); }
    }
    if (self.executionLine > 0)
    {
        NSRect r = [self rectOfLine:self.executionLine];
        if (!NSIsEmptyRect(r) && NSIntersectsRect(r, rect)) { [self.theme.execLine set]; NSRectFill(NSIntersectionRect(r, rect)); }
    }
}

- (void)setSelectedRanges:(NSArray<NSValue*>*)ranges affinity:(NSSelectionAffinity)affinity stillSelecting:(BOOL)still
{
    [super setSelectedRanges:ranges affinity:affinity stillSelecting:still];
    if (still) return;
    [self updateBraceMatch];
    [self setNeedsDisplayInRect:self.visibleRect avoidAdditionalLayout:YES];
    [self.gutter setNeedsDisplay:YES];
    if (self.caretMoved) self.caretMoved([self currentLine], [self currentColumn]);
}

- (BOOL)becomeFirstResponder
{
    BOOL ok = [super becomeFirstResponder];
    [self setNeedsDisplay:YES];
    return ok;
}

- (BOOL)resignFirstResponder
{
    BOOL ok = [super resignFirstResponder];
    [self setNeedsDisplay:YES];
    return ok;
}

- (void)textDidChangeHere:(NSNotification*)n
{
    (void)n;
    [self.gutter updateWidth];
    [self.gutter setNeedsDisplay:YES];
}

// ---- brace matching -------------------------------------------------------------------------
- (void)clearBraceMarks
{
    NSLayoutManager* lm = self.layoutManager;
    NSUInteger len = self.string.length;
    if (braceA.location != NSNotFound && NSMaxRange(braceA) <= len)
        [lm removeTemporaryAttribute:NSBackgroundColorAttributeName forCharacterRange:braceA];
    if (braceB.location != NSNotFound && NSMaxRange(braceB) <= len)
        [lm removeTemporaryAttribute:NSBackgroundColorAttributeName forCharacterRange:braceB];
    braceA = NSMakeRange(NSNotFound, 0);
    braceB = NSMakeRange(NSNotFound, 0);
}

static unichar Partner(unichar c)
{
    switch (c)
    {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    default:  return 0;
    }
}

- (void)updateBraceMatch
{
    [self clearBraceMarks];
    NSString* s = self.string;
    NSRange sel = self.selectedRange;
    if (sel.length != 0 || s.length == 0) return;
    NSUInteger at = NSNotFound;
    if (sel.location < s.length && Partner([s characterAtIndex:sel.location]) != 0) at = sel.location;
    else if (sel.location > 0 && Partner([s characterAtIndex:sel.location - 1]) != 0) at = sel.location - 1;
    if (at == NSNotFound) return;
    unichar c = [s characterAtIndex:at];
    unichar p = Partner(c);
    BOOL forward = (c == '(' || c == '[' || c == '{');
    int depth = 0;
    NSUInteger limit = 20000;
    NSUInteger match = NSNotFound;
    if (forward)
    {
        for (NSUInteger i = at; i < s.length && i - at < limit; i++)
        {
            unichar x = [s characterAtIndex:i];
            if (x == c) depth++;
            else if (x == p && --depth == 0) { match = i; break; }
        }
    }
    else
    {
        for (NSInteger i = (NSInteger)at; i >= 0 && at - (NSUInteger)i < limit; i--)
        {
            unichar x = [s characterAtIndex:(NSUInteger)i];
            if (x == c) depth++;
            else if (x == p && --depth == 0) { match = (NSUInteger)i; break; }
        }
    }
    if (match == NSNotFound || self.theme == nil) return;
    braceA = NSMakeRange(at, 1);
    braceB = NSMakeRange(match, 1);
    [self.layoutManager addTemporaryAttribute:NSBackgroundColorAttributeName value:self.theme.braceMatch forCharacterRange:braceA];
    [self.layoutManager addTemporaryAttribute:NSBackgroundColorAttributeName value:self.theme.braceMatch forCharacterRange:braceB];
}

// pasted text comes as plain text in the editor's font
- (NSArray<NSPasteboardType>*)readablePasteboardTypes { return @[ NSPasteboardTypeString ]; }
@end
