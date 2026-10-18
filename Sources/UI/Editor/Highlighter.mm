// =============================================================================
//  Highlighter.mm - see Highlighter.h
// =============================================================================
#import "Highlighter.h"
#include <set>
#include <string>

using GRIDEm::Core::Language;

namespace {

    bool IsLetter(unichar c)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
        if (c < 0x80) return false;
        static NSCharacterSet* letters = [NSCharacterSet letterCharacterSet];
        return [letters characterIsMember:c];
    }
    bool IsDigit(unichar c) { return c >= '0' && c <= '9'; }
    bool IsLetterOrDigit(unichar c) { return IsDigit(c) || IsLetter(c); }

    std::string Lower(const std::string& s)
    {
        std::string r = s;
        for (char& c : r) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return r;
    }

    // text[i...] starts with marker
    bool At(const unichar* text, NSUInteger length, NSUInteger i, NSString* marker)
    {
        NSUInteger n = marker.length;
        if (n == 0 || i + n > length) return false;
        for (NSUInteger k = 0; k < n; k++)
            if (text[i + k] != [marker characterAtIndex:k]) return false;
        return true;
    }

    NSUInteger Find(const unichar* text, NSUInteger length, NSString* marker, NSUInteger from)
    {
        NSUInteger n = marker.length;
        if (n == 0) return NSNotFound;
        for (NSUInteger i = from; i + n <= length; i++)
            if (At(text, length, i, marker)) return i;
        return NSNotFound;
    }

    NSUInteger FindChar(const unichar* text, NSUInteger length, unichar c, NSUInteger from)
    {
        for (NSUInteger i = from; i < length; i++)
            if (text[i] == c) return i;
        return NSNotFound;
    }
}

@implementation GRHighlighter
{
    GRIDEm::Settings::LanguageSettingsPtr lang;
    std::set<std::string> keywords;
    std::set<std::string> types;
    bool caseInsensitive;
    bool dotIdents;
    NSString* lineComment;
    NSString* blockStart;
    NSString* blockEnd;
    __weak NSTextView* view;
    BOOL scheduled;
}

- (instancetype)initWithLanguage:(GRIDEm::Settings::LanguageSettingsPtr)l
{
    self = [super init];
    if (self)
    {
        _theme = [[GRTheme current] resolvedFor:[NSApp effectiveAppearance]];
        [self setLanguage:l];
    }
    return self;
}

- (void)dealloc { [self detach]; }

- (void)setLanguage:(GRIDEm::Settings::LanguageSettingsPtr)l
{
    lang = l;
    keywords.clear();
    types.clear();
    caseInsensitive = l && (l->Lang == Language::Assembly || l->Lang == Language::LinkerCmd);
    dotIdents = caseInsensitive;
    if (l)
    {
        for (const std::string& k : l->Keywords) keywords.insert(caseInsensitive ? Lower(k) : k);
        for (const std::string& k : l->Types) types.insert(caseInsensitive ? Lower(k) : k);
        lineComment = [NSString stringWithUTF8String:l->LineComment.c_str()];
        blockStart = [NSString stringWithUTF8String:l->BlockCommentStart.c_str()];
        blockEnd = [NSString stringWithUTF8String:l->BlockCommentEnd.c_str()];
    }
    else
    {
        lineComment = @"";
        blockStart = @"";
        blockEnd = @"";
    }
    [self highlightVisible];
}

- (BOOL)isPlain { return !lang; }

// ---- scanning ------------------------------------------------------------------
- (BOOL)inBlockComment:(NSString*)text from:(NSUInteger)from pos:(NSUInteger)pos
{
    if (!lang || blockStart.length == 0) return NO;
    NSUInteger length = text.length;
    std::vector<unichar> buf(length + 1);
    [text getCharacters:buf.data() range:NSMakeRange(0, length)];
    const unichar* t = buf.data();
    bool inBlock = false;
    NSUInteger i = from;
    NSUInteger n = MIN(pos, length);
    while (i < n)
    {
        if (inBlock)
        {
            NSUInteger e = Find(t, length, blockEnd, i);
            if (e == NSNotFound || e >= n) return YES;
            i = e + blockEnd.length;
            inBlock = false;
            continue;
        }
        unichar c = t[i];
        if (c == '"' || c == '\'')                      // skip a string literal
        {
            NSUInteger j = i + 1;
            while (j < length && t[j] != c && t[j] != '\n') { if (t[j] == '\\') j++; j++; }
            i = j + 1;
            continue;
        }
        if (lineComment.length > 0 && At(t, length, i, lineComment))
        {
            NSUInteger e = FindChar(t, length, '\n', i);
            i = e == NSNotFound ? n : e + 1;
            continue;
        }
        if (At(t, length, i, blockStart)) { inBlock = true; i += blockStart.length; continue; }
        i++;
    }
    return inBlock ? YES : NO;
}

- (std::vector<GRToken>)tokenize:(NSString*)text start:(NSUInteger)start end:(NSUInteger)endIn
                  startInComment:(BOOL)startInComment
{
    std::vector<GRToken> r;
    if (!lang) return r;
    NSUInteger length = text.length;
    NSUInteger end = MIN(endIn, length);
    std::vector<unichar> buf(length + 1);
    [text getCharacters:buf.data() range:NSMakeRange(0, length)];
    const unichar* t = buf.data();
    bool hasBlock = blockStart.length > 0;
    NSUInteger i = start;

    if (startInComment && hasBlock)
    {
        NSUInteger e = Find(t, length, blockEnd, i);
        NSUInteger stop = e == NSNotFound ? end : MIN(end, e + blockEnd.length);
        r.push_back(GRToken{ i, stop - i, GRTokenComment });
        i = stop;
    }

    bool lineStart = i == 0 || t[i - 1] == '\n';
    while (i < end)
    {
        unichar c = t[i];
        if (c == '\n') { lineStart = true; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        // block comment
        if (hasBlock && At(t, length, i, blockStart))
        {
            NSUInteger e = Find(t, length, blockEnd, i + blockStart.length);
            NSUInteger stop = e == NSNotFound ? end : MIN(end, e + blockEnd.length);
            r.push_back(GRToken{ i, stop - i, GRTokenComment });
            i = stop;
            lineStart = false;
            continue;
        }
        // line comment
        if (lineComment.length > 0 && At(t, length, i, lineComment))
        {
            NSUInteger e = FindChar(t, length, '\n', i);
            NSUInteger stop = (e == NSNotFound || e > end) ? end : e;
            r.push_back(GRToken{ i, stop - i, GRTokenComment });
            i = stop;
            continue;
        }
        // preprocessor directive at the start of a line: #include, #define ...
        if (lang->Preprocessor && c == '#' && lineStart)
        {
            NSUInteger j = i + 1;
            while (j < end && (t[j] == ' ' || t[j] == '\t')) j++;
            while (j < end && IsLetter(t[j])) j++;
            r.push_back(GRToken{ i, j - i, GRTokenPreprocessor });
            // <file.h> after #include
            NSUInteger k = j;
            while (k < end && t[k] == ' ') k++;
            if (k < end && t[k] == '<')
            {
                NSUInteger e = FindChar(t, length, '>', k);
                NSUInteger nl = FindChar(t, length, '\n', k);
                if (e != NSNotFound && (nl == NSNotFound || e < nl) && e < end)
                {
                    r.push_back(GRToken{ k, e - k + 1, GRTokenString });
                    j = e + 1;
                }
            }
            i = j;
            lineStart = false;
            continue;
        }
        lineStart = false;

        // string / char literal
        if (c == '"' || (c == '\'' && !dotIdents))
        {
            NSUInteger j = i + 1;
            while (j < end && t[j] != c && t[j] != '\n') { if (t[j] == '\\') j++; j++; }
            if (j < end && t[j] == c) j++;
            if (j > end) j = end;
            r.push_back(GRToken{ i, j - i, GRTokenString });
            i = j;
            continue;
        }
        // number: 42, 0x1F, 3.14f, 1e-3, 10UL
        if (IsDigit(c) || (c == '.' && i + 1 < end && IsDigit(t[i + 1])))
        {
            NSUInteger j = i + 1;
            while (j < end && (IsLetterOrDigit(t[j]) || t[j] == '.'
                               || ((t[j] == '+' || t[j] == '-') && (t[j - 1] == 'e' || t[j - 1] == 'E'))))
                j++;
            r.push_back(GRToken{ i, j - i, GRTokenNumber });
            i = j;
            continue;
        }
        // identifier / keyword
        if (IsLetter(c) || c == '_' || (dotIdents && (c == '.' || c == '-')))
        {
            NSUInteger j = i + 1;
            while (j < end && (IsLetterOrDigit(t[j]) || t[j] == '_' || (dotIdents && t[j] == '.'))) j++;
            NSString* w = [text substringWithRange:NSMakeRange(i, j - i)];
            std::string word = w.UTF8String ? w.UTF8String : "";
            if (caseInsensitive) word = Lower(word);
            if (keywords.count(word)) r.push_back(GRToken{ i, j - i, GRTokenKeyword });
            else if (types.count(word)) r.push_back(GRToken{ i, j - i, GRTokenType });
            else if (!dotIdents)
            {
                NSUInteger k = j;
                while (k < end && (t[k] == ' ' || t[k] == '\t')) k++;
                if (k < end && t[k] == '(') r.push_back(GRToken{ i, j - i, GRTokenFunction });
            }
            i = j;
            continue;
        }
        i++;
    }
    return r;
}

// ---- painting --------------------------------------------------------------------
- (void)attachToTextView:(NSTextView*)textView
{
    [self detach];
    view = textView;
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(textChanged:) name:NSTextStorageDidProcessEditingNotification
             object:textView.textStorage];
    NSClipView* clip = textView.enclosingScrollView.contentView;
    if (clip != nil)
    {
        clip.postsBoundsChangedNotifications = YES;
        [nc addObserver:self selector:@selector(scrolled:) name:NSViewBoundsDidChangeNotification object:clip];
    }
    [nc addObserver:self selector:@selector(themeChanged:) name:GRThemeDidChangeNotification object:nil];
    [self highlightVisible];
}

- (void)detach
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    view = nil;
}

- (void)textChanged:(NSNotification*)n
{
    NSTextStorage* storage = n.object;
    if (!(storage.editedMask & NSTextStorageEditedCharacters)) return;
    [self schedule];
}

- (void)scrolled:(NSNotification*)n { (void)n; [self schedule]; }

- (void)themeChanged:(NSNotification*)n
{
    (void)n;
    NSTextView* v = view;
    _theme = [[GRTheme current] resolvedFor:(v != nil ? v.effectiveAppearance : [NSApp effectiveAppearance])];
    [self schedule];
}

// one colouring per run of the main loop, however many edits came in it
- (void)schedule
{
    if (scheduled) return;
    scheduled = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        self->scheduled = NO;
        [self highlightVisible];
    });
}

- (void)paintRange:(NSRange)range
{
    NSTextView* v = view;
    if (v == nil) return;
    NSLayoutManager* lm = v.layoutManager;
    NSString* text = v.string;
    if (lm == nil || range.length == 0 || NSMaxRange(range) > text.length) return;
    [lm removeTemporaryAttribute:NSForegroundColorAttributeName forCharacterRange:range];
    [lm addTemporaryAttribute:NSForegroundColorAttributeName value:self.theme.editorText forCharacterRange:range];
    if (!lang) return;
    // a block comment may have opened before the range: scan from far enough back
    NSUInteger from = 0;
    BOOL inComment = [self inBlockComment:text from:from pos:range.location];
    std::vector<GRToken> tokens = [self tokenize:text start:range.location end:NSMaxRange(range) startInComment:inComment];
    for (const GRToken& tk : tokens)
    {
        if (tk.length == 0) continue;
        NSRange r = NSIntersectionRange(NSMakeRange(tk.start, tk.length), range);
        if (r.length == 0) continue;
        [lm addTemporaryAttribute:NSForegroundColorAttributeName value:[self.theme colorForToken:tk.kind] forCharacterRange:r];
    }
}

- (void)highlightVisible
{
    NSTextView* v = view;
    if (v == nil || v.layoutManager == nil || v.textContainer == nil) return;
    NSRect visible = v.visibleRect;
    NSRange glyphs = [v.layoutManager glyphRangeForBoundingRect:visible inTextContainer:v.textContainer];
    NSRange chars = [v.layoutManager characterRangeForGlyphRange:glyphs actualGlyphRange:nullptr];
    // whole lines, so a token is never cut at the edge
    NSString* text = v.string;
    if (chars.location > text.length) return;
    if (NSMaxRange(chars) > text.length) chars.length = text.length - chars.location;
    chars = [text lineRangeForRange:chars];
    [self paintRange:chars];
}

- (void)highlightAll
{
    NSTextView* v = view;
    if (v == nil) return;
    [self paintRange:NSMakeRange(0, v.string.length)];
}
@end
