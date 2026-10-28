// =============================================================================
//  OutputView.mm - see OutputView.h
// =============================================================================
#import "OutputView.h"
#import "PaneUtil.h"
#import "../Theme.h"
#include "../../Services/BuildMessage.h"

using namespace GRIDEm;

@interface GROutputTextView : NSTextView
@property (nonatomic, copy) void (^doubleClicked)(NSUInteger characterIndex);
@end

@implementation GROutputTextView
- (void)mouseDown:(NSEvent*)event
{
    [super mouseDown:event];
    if (event.clickCount == 2 && self.doubleClicked)
    {
        NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
        NSUInteger i = [self characterIndexForInsertionAtPoint:p];
        self.doubleClicked(i);
    }
}
@end

@implementation GROutputView
{
    NSMutableDictionary<NSString*, NSMutableArray<NSString*>*>* _lines;
    NSPopUpButton* _source;
    GROutputTextView* _text;
    NSScrollView* _scroll;
    NSTextField* _input;
    BOOL _inputEnabled;
}

- (instancetype)init
{
    self = [super initWithIdentifier:@"Output" title:@"Output" symbol:@"text.alignleft"];
    if (self != nil)
    {
        _lines = [NSMutableDictionary dictionary];
        _echoInput = YES;
    }
    return self;
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    NSTextField* label = [GRPaneUtil label:@"Show output from:"];
    _source = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_source addItemsWithTitles:@[ @"Build", @"Debug", @"Tools" ]];
    _source.controlSize = NSControlSizeSmall;
    _source.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    _source.target = self;
    _source.action = @selector(sourceChanged:);
    NSButton* clear = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"trash" accessibilityDescription:@"Clear"]
                                         target:self action:@selector(clearCurrent:)];
    clear.bordered = NO;
    clear.toolTip = @"Clear";
    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ label, _source, spacer, clear ]];

    // a text view of our own class in the scroll view, so a double-click can be heard
    _scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 600, 160)];
    _scroll.hasVerticalScroller = YES;
    _scroll.hasHorizontalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.borderType = NSNoBorder;
    NSSize size = _scroll.contentSize;
    _text = [[GROutputTextView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    _text.editable = NO;
    _text.selectable = YES;
    _text.richText = YES;
    _text.font = [GRPaneUtil monoFont];
    _text.textContainerInset = NSMakeSize(4, 4);
    _text.horizontallyResizable = YES;
    _text.verticallyResizable = YES;
    _text.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    _text.minSize = NSMakeSize(0, size.height);
    _text.autoresizingMask = NSViewWidthSizable;
    _text.textContainer.widthTracksTextView = NO;
    _text.textContainer.containerSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    __weak GROutputView* weakSelf = self;
    _text.doubleClicked = ^(NSUInteger i) { [weakSelf goToLineAt:i]; };
    _scroll.documentView = _text;

    _input = [GRPaneUtil inputFieldWithPlaceholder:@"input for the running program - Return sends it"];
    _input.delegate = self;
    _input.hidden = YES;

    [GRPaneUtil stack:@[ bar, _scroll, _input ] in:root grow:_scroll];
    self.view = root;
    [self applyTheme];
}

- (NSString*)currentSource
{
    if (!self.isViewLoaded) return @"Build";
    NSString* s = _source.titleOfSelectedItem;
    return s != nil ? s : @"Build";
}

- (BOOL)inputEnabled { return _inputEnabled; }

- (void)setInputEnabled:(BOOL)on { [self showInput:on focus:YES]; }

- (void)showInput:(BOOL)on focus:(BOOL)focus
{
    BOOL wasShown = _inputEnabled;
    _inputEnabled = on;
    if (!self.isViewLoaded) return;
    if (on && !wasShown) _input.stringValue = @"";
    _input.hidden = !on;
    if (on && focus) [_input.window makeFirstResponder:_input];
}

- (NSMutableArray<NSString*>*)get:(NSString*)source
{
    NSMutableArray* l = _lines[source];
    if (l == nil) { l = [NSMutableArray array]; _lines[source] = l; }
    return l;
}

- (NSArray<NSString*>*)lines:(NSString*)source { return [[self get:source] copy]; }

+ (NSString*)plainText:(NSString*)line
{
    if ([line rangeOfString:@"\x1b"].location == NSNotFound) return line;
    NSMutableString* out = [NSMutableString string];
    NSUInteger n = line.length, i = 0;
    while (i < n)
    {
        unichar c = [line characterAtIndex:i];
        if (c != 0x1b) { [out appendFormat:@"%C", c]; i++; continue; }
        i++;
        if (i < n && [line characterAtIndex:i] == '[')
        {
            i++;
            while (i < n)
            {
                unichar d = [line characterAtIndex:i++];
                if (d >= 0x40 && d <= 0x7e) break;
            }
        }
        else if (i < n && [line characterAtIndex:i] == ']')
        {
            // OSC: up to BEL or ESC backslash
            i++;
            while (i < n)
            {
                unichar d = [line characterAtIndex:i++];
                if (d == 0x07) break;
                if (d == 0x1b && i < n && [line characterAtIndex:i] == '\\') { i++; break; }
            }
        }
        else if (i < n) i++;
    }
    return out;
}

- (void)append:(NSString*)text source:(NSString*)source
{
    if (![NSThread isMainThread])
    {
        dispatch_async(dispatch_get_main_queue(), ^{ [self append:text source:source]; });
        return;
    }
    NSString* line = [GROutputView plainText:text != nil ? text : @""];
    NSMutableArray* l = [self get:source];
    [l addObject:line];
    BOOL trimmed = NO;
    if (l.count > 20000) { [l removeObjectsInRange:NSMakeRange(0, 5000)]; trimmed = YES; }
    if (self.isViewLoaded && [source isEqualToString:self.currentSource])
    {
        if (trimmed) [self redraw];
        else [self appendLine:line];
    }
}

- (void)continueLine:(NSString*)text source:(NSString*)source
{
    if (![NSThread isMainThread])
    {
        dispatch_async(dispatch_get_main_queue(), ^{ [self continueLine:text source:source]; });
        return;
    }
    NSMutableArray* l = [self get:source];
    if (l.count == 0) { [self append:text source:source]; return; }
    NSString* rest = [GROutputView plainText:text != nil ? text : @""];
    l[l.count - 1] = [l.lastObject stringByAppendingString:rest];
    if (self.isViewLoaded && [source isEqualToString:self.currentSource]) [self redraw];
}

- (void)clear:(NSString*)source
{
    [[self get:source] removeAllObjects];
    if (self.isViewLoaded && [source isEqualToString:self.currentSource]) [_text.textStorage setAttributedString:[NSAttributedString new]];
}

- (void)showSource:(NSString*)source
{
    if (!self.isViewLoaded) [self loadView];
    if ([_source indexOfItemWithTitle:source] < 0) return;
    [_source selectItemWithTitle:source];
    [self redraw];
}

- (void)sourceChanged:(id)sender { (void)sender; [self redraw]; }
- (void)clearCurrent:(id)sender { (void)sender; [self clear:self.currentSource]; }

// GRIDEd's ColorFor
- (NSColor*)colorFor:(NSString*)s theme:(GRTheme*)t
{
    NSString* low = s.lowercaseString;
    if ([low hasPrefix:@"------"] || [low hasPrefix:@"======"])
        return [low rangeOfString:@" 0 failed"].location != NSNotFound ? t.successText : t.accent;
    static NSRegularExpression* error = nil;
    static NSRegularExpression* warning = nil;
    if (error == nil)
    {
        error = [NSRegularExpression regularExpressionWithPattern:@"\\berror\\b|fatal" options:0 error:nil];
        warning = [NSRegularExpression regularExpressionWithPattern:@"\\bwarning\\b" options:0 error:nil];
    }
    NSRange all = NSMakeRange(0, low.length);
    if ([error firstMatchInString:low options:0 range:all] != nil) return t.errorText;
    if ([warning firstMatchInString:low options:0 range:all] != nil) return t.warningText;
    return t.text;
}

- (void)appendLine:(NSString*)line
{
    GRTheme* t = [[GRTheme current] resolvedFor:_text.effectiveAppearance];
    NSDictionary* attrs = @{ NSFontAttributeName: [GRPaneUtil monoFont],
                             NSForegroundColorAttributeName: [self colorFor:line theme:t] };
    NSAttributedString* a = [[NSAttributedString alloc] initWithString:[line stringByAppendingString:@"\n"] attributes:attrs];
    // follow the end only when the view was at the end
    NSRect visible = _scroll.contentView.documentVisibleRect;
    BOOL atEnd = NSMaxY(visible) >= NSHeight(_text.bounds) - 4;
    [_text.textStorage appendAttributedString:a];
    if (atEnd) [_text scrollRangeToVisible:NSMakeRange(_text.string.length, 0)];
}

- (void)redraw
{
    if (!self.isViewLoaded) return;
    GRTheme* t = [[GRTheme current] resolvedFor:_text.effectiveAppearance];
    _text.backgroundColor = t.window;
    NSMutableAttributedString* all = [[NSMutableAttributedString alloc] init];
    NSFont* font = [GRPaneUtil monoFont];
    for (NSString* line in [self get:self.currentSource])
    {
        NSDictionary* attrs = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: [self colorFor:line theme:t] };
        [all appendAttributedString:[[NSAttributedString alloc] initWithString:[line stringByAppendingString:@"\n"]
                                                                    attributes:attrs]];
    }
    [_text.textStorage setAttributedString:all];
    [_text scrollRangeToVisible:NSMakeRange(_text.string.length, 0)];
}

- (void)applyTheme
{
    [super applyTheme];
    [self redraw];
}

// a double-click goes to the place the line names (GRIDEd's OnDoubleClick)
- (void)goToLineAt:(NSUInteger)index
{
    NSString* s = _text.string;
    if (index > s.length) return;
    NSRange lineRange = [s lineRangeForRange:NSMakeRange(index, 0)];
    NSString* line = [[s substringWithRange:lineRange] stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    std::string l8 = GRStd(line);
    Services::BuildMessagePtr m = Services::BuildMessageParser::TryParse(l8, std::string());
    if (!m || m->Line <= 0) m = Services::BuildMessageParser::TryParseLocation(l8, std::string());
    if (!m || m->Line <= 0) return;
    if (self.navigate) self.navigate(GRNS(m->File), m->Line, m->Column);
}

// Return hands the line to the program and shows it, as a console echoes what is typed
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (control != _input || commandSelector != @selector(insertNewline:)) return NO;
    NSString* text = _input.stringValue;
    _input.stringValue = @"";
    if (self.echoInput) [self append:text source:self.currentSource];
    if (self.inputEntered) self.inputEntered(text);
    return YES;
}
@end
