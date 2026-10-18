// =============================================================================
//  Gutter.mm - see Gutter.h
// =============================================================================
#import "Gutter.h"
#import "CodeView.h"
#import "SourceDocument.h"
#import "../Theme.h"
#import "Highlighter.h"
#include <cmath>

static const CGFloat kMarkArea = 16;       // breakpoints, the arrow, build marks
static const CGFloat kBookmarkArea = 5;    // the bookmark bar at the right edge

@implementation GRGutter
{
    NSInteger pressedLine;
    BOOL draggedOff;
    NSTrackingArea* tracking;
}

- (instancetype)initWithCodeView:(GRCodeView*)codeView
{
    self = [super initWithScrollView:codeView.enclosingScrollView orientation:NSVerticalRuler];
    if (self)
    {
        _codeView = codeView;
        _showNumbers = YES;
        self.clientView = codeView;
        pressedLine = 0;
        [self updateWidth];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (GRTheme*)theme
{
    GRTheme* t = self.codeView.theme;
    return t != nil ? t : [[GRTheme current] resolvedFor:self.effectiveAppearance];
}

- (NSFont*)numberFont
{
    NSFont* f = self.codeView.font;
    CGFloat size = f != nil ? MAX(8.0, f.pointSize - 1.0) : 11.0;
    return [NSFont monospacedDigitSystemFontOfSize:size weight:NSFontWeightRegular];
}

- (void)setShowNumbers:(BOOL)on
{
    _showNumbers = on;
    [self updateWidth];
}

- (void)updateWidth
{
    CGFloat width = kMarkArea + kBookmarkArea;
    if (self.showNumbers)
    {
        NSInteger lines = MAX(1, [self.codeView lineCount]);
        int digits = MAX(2, (int)std::floor(std::log10((double)lines)) + 1);
        CGFloat digit = [@"0" sizeWithAttributes:@{ NSFontAttributeName : [self numberFont] }].width;
        width += digits * digit + 6;
    }
    width = std::ceil(width);
    if (std::fabs(self.ruleThickness - width) > 0.5) self.ruleThickness = width;
    [self setNeedsDisplay:YES];
}

- (void)setMarks:(std::map<int, GRMarkKind>)marks
{
    _marks = marks;
    [self setNeedsDisplay:YES];
}

- (void)clearMarks
{
    _marks.clear();
    self.markMessages = nil;
    [self setNeedsDisplay:YES];
}

// ---- drawing -------------------------------------------------------------------------
- (void)drawHashMarksAndLabelsInRect:(NSRect)rect
{
    GRCodeView* v = self.codeView;
    GRTheme* t = [self theme];
    [t.window set];
    NSRectFill(rect);
    [t.border set];
    NSRectFill(NSMakeRect(NSMaxX(self.bounds) - 1, NSMinY(rect), 1, rect.size.height));
    if (v == nil || v.layoutManager == nil) return;

    NSLayoutManager* lm = v.layoutManager;
    NSTextContainer* tc = v.textContainer;
    NSString* s = v.string;
    GRSourceDocument* d = v.document;
    std::shared_ptr<std::set<int> > bp = d != nil ? d.breakpoints : std::shared_ptr<std::set<int> >();
    std::set<int> bookmarks = d != nil ? d.bookmarks : std::set<int>();
    NSInteger current = [v currentLine];
    NSDictionary* numberAttrs = @{ NSFontAttributeName : [self numberFont], NSForegroundColorAttributeName : t.lineNumber };
    NSDictionary* currentAttrs = @{ NSFontAttributeName : [self numberFont], NSForegroundColorAttributeName : t.editorText };
    CGFloat numberRight = NSMaxX(self.bounds) - kBookmarkArea - 2;

    // the visible lines of the text view
    NSRect visible = v.visibleRect;
    NSRange glyphs = [lm glyphRangeForBoundingRect:visible inTextContainer:tc];
    NSRange chars = [lm characterRangeForGlyphRange:glyphs actualGlyphRange:nullptr];
    NSInteger line = [v lineOfCharacter:chars.location];
    NSUInteger index = [s lineRangeForRange:NSMakeRange(MIN(chars.location, s.length), 0)].location;
    CGFloat originY = v.textContainerOrigin.y;

    void (^drawLine)(NSInteger, NSRect) = ^(NSInteger ln, NSRect frag) {
        NSPoint top = [self convertPoint:NSMakePoint(0, frag.origin.y + originY) fromView:v];
        CGFloat y = top.y;
        CGFloat h = frag.size.height;
        if (y + h < NSMinY(rect) || y > NSMaxY(rect)) return;
        int l = (int)ln;
        // build marks
        std::map<int, GRMarkKind>::const_iterator m = self->_marks.find(l);
        if (m != self->_marks.end())
        {
            [(m->second == GRMarkError ? t.errorText : t.warningText) set];
            NSRectFill(NSMakeRect(0, y + 1, 3, h - 2));
        }
        // breakpoint
        if (bp && bp->count(l))
        {
            CGFloat size = MIN(h - 2, 12);
            NSRect dot = NSMakeRect((kMarkArea - size) / 2 + 1, y + (h - size) / 2, size, size);
            [t.breakpoint set];
            [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
        }
        // the debugger's arrow
        if (v.executionLine == ln)
        {
            CGFloat mid = y + h / 2;
            NSBezierPath* arrow = [NSBezierPath bezierPath];
            [arrow moveToPoint:NSMakePoint(3, mid - 3)];
            [arrow lineToPoint:NSMakePoint(9, mid - 3)];
            [arrow lineToPoint:NSMakePoint(9, mid - 6)];
            [arrow lineToPoint:NSMakePoint(15, mid)];
            [arrow lineToPoint:NSMakePoint(9, mid + 6)];
            [arrow lineToPoint:NSMakePoint(9, mid + 3)];
            [arrow lineToPoint:NSMakePoint(3, mid + 3)];
            [arrow closePath];
            [t.accent set];
            [arrow fill];
        }
        // bookmark
        if (bookmarks.count(l))
        {
            [t.bookmark set];
            NSRectFill(NSMakeRect(NSMaxX(self.bounds) - kBookmarkArea, y + 1, kBookmarkArea - 1, h - 2));
        }
        // the number
        if (self.showNumbers)
        {
            NSString* n = [NSString stringWithFormat:@"%ld", (long)ln];
            NSDictionary* a = ln == current ? currentAttrs : numberAttrs;
            NSSize ns = [n sizeWithAttributes:a];
            [n drawAtPoint:NSMakePoint(numberRight - ns.width, y + (h - ns.height) / 2) withAttributes:a];
        }
    };

    while (index < s.length && index <= NSMaxRange(chars))
    {
        NSUInteger glyph = [lm glyphIndexForCharacterAtIndex:index];
        NSRect frag = [lm lineFragmentRectForGlyphAtIndex:glyph effectiveRange:nullptr];
        drawLine(line, frag);
        NSRange lr = [s lineRangeForRange:NSMakeRange(index, 0)];
        if (NSMaxRange(lr) <= index) break;
        index = NSMaxRange(lr);
        line++;
    }
    // the empty last line after a final line end, or an empty file
    if (index >= s.length && (s.length == 0 || [s characterAtIndex:s.length - 1] == '\n'))
    {
        NSRect frag = lm.extraLineFragmentRect;
        if (frag.size.height > 0) drawLine(line, frag);
    }
}

// ---- clicking ----------------------------------------------------------------------------
// the line under a point of the ruler, 0 when below the text
- (NSInteger)lineAt:(NSPoint)p
{
    GRCodeView* v = self.codeView;
    if (v == nil) return 0;
    NSPoint inView = [v convertPoint:p fromView:self];
    NSLayoutManager* lm = v.layoutManager;
    NSString* s = v.string;
    CGFloat y = inView.y - v.textContainerOrigin.y;
    NSRect extra = lm.extraLineFragmentRect;
    if (extra.size.height > 0 && y >= NSMinY(extra) && y < NSMaxY(extra)) return [v lineCount];
    if (s.length == 0) return 1;
    NSUInteger glyph = [lm glyphIndexForPoint:NSMakePoint(0, y) inTextContainer:v.textContainer fractionOfDistanceThroughGlyph:nullptr];
    NSRect frag = [lm lineFragmentRectForGlyphAtIndex:glyph effectiveRange:nullptr];
    if (y > NSMaxY(frag) + 1) return 0;
    NSUInteger ch = [lm characterIndexForGlyphAtIndex:glyph];
    return [v lineOfCharacter:ch];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    pressedLine = [self lineAt:p];
    draggedOff = NO;
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    // a breakpoint dragged well away from the gutter is deleted on release
    if (p.x > NSMaxX(self.bounds) + 24 || p.x < -24) draggedOff = YES;
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    GRCodeView* v = self.codeView;
    NSInteger line = pressedLine;
    pressedLine = 0;
    if (v == nil || line <= 0) return;
    GRSourceDocument* d = v.document;
    std::shared_ptr<std::set<int> > bp = d != nil ? d.breakpoints : std::shared_ptr<std::set<int> >();
    BOOL has = bp && bp->count((int)line);
    if (draggedOff && !has) return;
    [v toggleBreakpoint:line];
}

// ---- the message of a marked line ------------------------------------------------------
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (tracking != nil) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                            options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                              owner:self userInfo:nil];
    [self addTrackingArea:tracking];
}

- (void)mouseMoved:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger line = [self lineAt:p];
    NSString* msg = line > 0 ? self.markMessages[@(line)] : nil;
    if (msg != self.toolTip && ![msg isEqualToString:self.toolTip]) self.toolTip = msg;
}
@end
