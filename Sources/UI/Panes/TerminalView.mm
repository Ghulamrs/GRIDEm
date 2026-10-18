// =============================================================================
//  TerminalView.mm - see TerminalView.h
// =============================================================================
#import "TerminalView.h"
#import "PaneUtil.h"
#import "../Theme.h"
#include "../../Core/TextFile.h"
#include "../../Platform/Paths.h"
#include "../../Platform/Process.h"
#include <memory>
#include <string>
#include <vector>

using namespace GRIDEm;

// -----------------------------------------------------------------------------
//  the screen: lines of text and a cursor, fed with what the shell writes
// -----------------------------------------------------------------------------
@interface GRTerminalScreen : NSObject
@property (nonatomic, readonly) NSMutableArray<NSMutableString*>* lines;
@property (nonatomic, assign) NSUInteger row;
@property (nonatomic, assign) NSUInteger column;
- (void)feed:(NSString*)text;
- (void)reset;
@end

@implementation GRTerminalScreen
{
    int _state;                 // 0 text, 1 after ESC, 2 in CSI, 3 in OSC, 4 OSC after ESC
    NSMutableString* _params;
}

- (instancetype)init
{
    self = [super init];
    if (self != nil)
    {
        _lines = [NSMutableArray array];
        _params = [NSMutableString string];
        [self reset];
    }
    return self;
}

- (void)reset
{
    [_lines removeAllObjects];
    [_lines addObject:[NSMutableString string]];
    _row = 0;
    _column = 0;
    _state = 0;
}

- (NSMutableString*)currentLine
{
    while (_row >= _lines.count) [_lines addObject:[NSMutableString string]];
    return _lines[_row];
}

- (void)put:(unichar)c
{
    NSMutableString* l = [self currentLine];
    while (l.length < _column) [l appendString:@" "];
    NSString* s = [NSString stringWithCharacters:&c length:1];
    if (_column < l.length) [l replaceCharactersInRange:NSMakeRange(_column, 1) withString:s];
    else [l appendString:s];
    _column++;
}

- (void)newline
{
    _row++;
    _column = 0;
    [self currentLine];
    // the newest 5,000 lines are kept
    if (_lines.count > 5000)
    {
        NSUInteger drop = _lines.count - 5000;
        [_lines removeObjectsInRange:NSMakeRange(0, drop)];
        _row = _row >= drop ? _row - drop : 0;
    }
}

- (NSInteger)firstNumber:(NSInteger)fallback
{
    NSInteger v = 0;
    BOOL any = NO;
    for (NSUInteger i = 0; i < _params.length; i++)
    {
        unichar d = [_params characterAtIndex:i];
        if (d < '0' || d > '9') break;
        v = v * 10 + (d - '0');
        any = YES;
    }
    return any ? v : fallback;
}

- (void)csi:(unichar)final
{
    NSMutableString* l = [self currentLine];
    switch (final)
    {
    case 'K':                                   // erase in line: 0 to the end, 1 to the start, 2 all
    {
        NSInteger n = [self firstNumber:0];
        if (n == 0 && _column < l.length) [l deleteCharactersInRange:NSMakeRange(_column, l.length - _column)];
        else if (n == 2) [l setString:@""];
        else if (n == 1)
            for (NSUInteger i = 0; i < _column && i < l.length; i++) [l replaceCharactersInRange:NSMakeRange(i, 1) withString:@" "];
        break;
    }
    case 'J':                                   // clear the screen (2, 3) - the scrollback goes too
        if ([self firstNumber:0] >= 2) [self reset];
        break;
    case 'C': _column += (NSUInteger)[self firstNumber:1]; break;
    case 'D':
    {
        NSUInteger n = (NSUInteger)[self firstNumber:1];
        _column = _column > n ? _column - n : 0;
        break;
    }
    case 'G': { NSInteger c = [self firstNumber:1]; _column = c > 0 ? (NSUInteger)(c - 1) : 0; break; }
    case 'H':
    case 'f':
        // a move to the top left after a clear: the start of the current line, best effort
        if (_params.length == 0) _column = 0;
        break;
    default: break;                             // colours (m) and the rest are dropped
    }
}

- (void)feed:(NSString*)text
{
    NSUInteger n = text.length;
    for (NSUInteger i = 0; i < n; i++)
    {
        unichar c = [text characterAtIndex:i];
        switch (_state)
        {
        case 0:
            if (c == 0x1b) { _state = 1; continue; }
            if (c == '\n') { [self newline]; continue; }
            if (c == '\r') { _column = 0; continue; }
            if (c == 0x08) { if (_column > 0) _column--; continue; }
            if (c == 0x07) continue;
            if (c == '\t') { NSUInteger next = (_column / 8 + 1) * 8; while (_column < next) [self put:' ']; continue; }
            if (c < 0x20) continue;
            [self put:c];
            continue;
        case 1:
            if (c == '[') { _state = 2; [_params setString:@""]; }
            else if (c == ']') _state = 3;
            else _state = 0;
            continue;
        case 2:
            if (c >= 0x40 && c <= 0x7e) { [self csi:c]; _state = 0; }
            else [_params appendFormat:@"%C", c];
            continue;
        case 3:
            if (c == 0x07) _state = 0;
            else if (c == 0x1b) _state = 4;
            continue;
        default:
            _state = c == '\\' ? 0 : 3;
            continue;
        }
    }
}
@end

// -----------------------------------------------------------------------------
//  the view: shows the screen, sends what is typed
// -----------------------------------------------------------------------------
@interface GRTerminalTextView : NSTextView
@property (nonatomic, copy) void (^typed)(NSString* bytes);
@end

@implementation GRTerminalTextView
- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent*)event
{
    if (self.typed == nil) { [super keyDown:event]; return; }
    NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if (mods & NSEventModifierFlagCommand) { [super keyDown:event]; return; }   // Copy, Select All ...
    NSString* chars = event.characters;
    if (chars.length == 0) return;
    unichar c = [chars characterAtIndex:0];
    NSString* send = nil;
    switch (c)
    {
    case NSCarriageReturnCharacter:
    case NSEnterCharacter:       send = @"\r"; break;
    case NSDeleteCharacter:      send = @"\x7f"; break;
    case NSUpArrowFunctionKey:   send = @"\x1b[A"; break;
    case NSDownArrowFunctionKey: send = @"\x1b[B"; break;
    case NSRightArrowFunctionKey:send = @"\x1b[C"; break;
    case NSLeftArrowFunctionKey: send = @"\x1b[D"; break;
    case NSHomeFunctionKey:      send = @"\x1b[H"; break;
    case NSEndFunctionKey:       send = @"\x1b[F"; break;
    case NSDeleteFunctionKey:    send = @"\x1b[3~"; break;
    default:
        if (c >= 0xF700 && c <= 0xF8FF) return;           // other function keys
        send = chars;                                     // Control keys come as their control characters
        break;
    }
    self.typed(send);
}

- (void)paste:(id)sender
{
    (void)sender;
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (s.length > 0 && self.typed) self.typed(s);
}
@end

// -----------------------------------------------------------------------------
@implementation GRTerminalView
{
    GRTerminalTextView* _text;
    NSScrollView* _scroll;
    GRTerminalScreen* _screen;
    std::unique_ptr<Platform::Process> _shell;
    std::string _pending;           // UTF-8 bytes of a character cut by a read
    BOOL _started;
    BOOL _redrawQueued;
    NSTextField* _status;
}

- (instancetype)init
{
    self = [super initWithIdentifier:@"Terminal" title:@"Terminal" symbol:@"terminal"];
    if (self != nil) _screen = [[GRTerminalScreen alloc] init];
    return self;
}

- (void)dealloc
{
    [self stopShell];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    _status = [GRPaneUtil label:@""];
    _status.textColor = [NSColor secondaryLabelColor];
    NSButton* restart = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"arrow.clockwise" accessibilityDescription:@"Restart"]
                                           target:self action:@selector(restartPressed:)];
    restart.bordered = NO;
    restart.toolTip = @"Restart the shell";
    NSButton* clear = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"trash" accessibilityDescription:@"Clear"]
                                         target:self action:@selector(clearPressed:)];
    clear.bordered = NO;
    clear.toolTip = @"Clear";
    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _status, spacer, restart, clear ]];

    _scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 600, 180)];
    _scroll.hasVerticalScroller = YES;
    _scroll.autohidesScrollers = YES;
    _scroll.borderType = NSNoBorder;
    NSSize size = _scroll.contentSize;
    _text = [[GRTerminalTextView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    _text.editable = NO;
    _text.selectable = YES;
    _text.richText = NO;
    _text.font = [GRPaneUtil monoFont];
    _text.textContainerInset = NSMakeSize(4, 4);
    _text.verticallyResizable = YES;
    _text.horizontallyResizable = NO;
    _text.autoresizingMask = NSViewWidthSizable;
    _text.textContainer.widthTracksTextView = YES;
    __weak GRTerminalView* weakSelf = self;
    _text.typed = ^(NSString* s) { [weakSelf send:s]; };
    _scroll.documentView = _text;
    [GRPaneUtil stack:@[ bar, _scroll ] in:root grow:_scroll];
    self.view = root;
    [self applyTheme];
}

- (void)paneDidShow
{
    if (!_started) [self startShell];
    [_text.window makeFirstResponder:_text];
}

- (BOOL)running { return _shell && _shell->Running(); }

- (void)startShell
{
    if (!self.isViewLoaded) [self loadView];
    if (self.running) return;
    _started = YES;
    std::string shell;
    if (!Platform::Paths::GetEnvironment("SHELL", shell) || shell.empty()) shell = "/bin/zsh";
    std::string dir = GRStd(self.workingDirectory);
    if (dir.empty() || !Platform::Paths::IsDirectory(dir)) dir = Platform::Paths::HomeDirectory();
    _shell.reset(new Platform::Process());
    __weak GRTerminalView* weakSelf = self;
    _shell->OnOutput = [weakSelf](const std::string& bytes) {
        std::string copy = bytes;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf heard:copy]; });
    };
    std::vector<std::string> argv;
    argv.push_back(shell);
    argv.push_back("-l");
    std::vector<std::string> env;
    env.push_back("TERM=dumb");          // no colours, no fancy prompt editing
    env.push_back("TERM_PROGRAM=GRIDEm");
    std::string why;
    if (!_shell->Start(argv, dir, env, Platform::ProcessMode::Terminal, false, why))
    {
        [_screen feed:[NSString stringWithFormat:@"[cannot start %@: %@]\n", GRNS(shell), GRNS(why)]];
        _shell.reset();
        [self redraw];
        _status.stringValue = @"not running";
        return;
    }
    _status.stringValue = [NSString stringWithFormat:@"%@ - %@", GRNS(Platform::Paths::FileName(shell)), GRNS(dir)];
}

- (void)stopShell
{
    if (!_shell) return;
    _shell->End(500);
    _shell->Wait(1000);
    _shell.reset();
    if (self.isViewLoaded) _status.stringValue = @"stopped";
}

- (void)restart
{
    [self stopShell];
    [self clear];
    [self startShell];
}

- (void)clear
{
    [_screen reset];
    [self redraw];
}

- (void)restartPressed:(id)sender { (void)sender; [self restart]; }
- (void)clearPressed:(id)sender { (void)sender; [self clear]; }

- (void)send:(NSString*)text
{
    if (!self.running) { if (_started) [self startShell]; if (!self.running) return; }
    _shell->Write(GRStd(text));
}

// bytes from the shell, on the main thread: whole UTF-8 characters go to the screen
- (void)heard:(const std::string&)bytes
{
    _pending += bytes;
    size_t cut = _pending.size();
    // hold back a character whose bytes have not all come
    size_t back = 0;
    while (back < 4 && back < cut && ((unsigned char)_pending[cut - 1 - back] & 0xC0) == 0x80) back++;
    if (back < cut)
    {
        unsigned char lead = (unsigned char)_pending[cut - 1 - back];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (need > back + 1) cut = cut - 1 - back;
    }
    std::string whole = _pending.substr(0, cut);
    _pending.erase(0, cut);
    Core::FileEncoding enc;
    std::string text = Core::TextFile::Decode(whole, enc);
    [_screen feed:GRNS(text)];
    if (!_redrawQueued)
    {
        _redrawQueued = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_redrawQueued = NO;
            [self redraw];
        });
    }
    if (_shell && !_shell->Running() && self.isViewLoaded) _status.stringValue = @"the shell has ended - Restart starts another";
}

- (void)redraw
{
    if (!self.isViewLoaded) return;
    NSString* all = [_screen.lines componentsJoinedByString:@"\n"];
    GRTheme* t = [[GRTheme current] resolvedFor:_text.effectiveAppearance];
    NSDictionary* attrs = @{ NSFontAttributeName: [GRPaneUtil monoFont], NSForegroundColorAttributeName: t.editorText };
    [_text.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:all attributes:attrs]];
    [_text scrollRangeToVisible:NSMakeRange(all.length, 0)];
}

- (void)applyTheme
{
    [super applyTheme];
    if (!self.isViewLoaded) return;
    GRTheme* t = [[GRTheme current] resolvedFor:_text.effectiveAppearance];
    _text.backgroundColor = t.window;
    _text.insertionPointColor = t.editorText;
    [self redraw];
}
@end
