// =============================================================================
//  MemoryView.mm - see MemoryView.h
// =============================================================================
#import "MemoryView.h"
#import "../Common.h"
#import "PaneUtil.h"
#import "../Theme.h"
#include "../../Core/StringUtil.h"

using namespace GRIDEm;

@implementation GRMemoryView
{
    NSTextField* _address;
    NSTextView* _view;
    BOOL _chosen;              // an address was typed: kept from stop to stop
    BOOL _paused;
    std::vector<Services::DebugVariable> _locals;
    NSString* _backend;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"Memory" title:@"Memory" symbol:@"memorychip"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    NSTextField* label = [GRPaneUtil label:@"Address:"];
    _address = [GRPaneUtil inputFieldWithPlaceholder:@"0x..."];
    _address.delegate = self;
    [_address.widthAnchor constraintEqualToConstant:170].active = YES;
    NSButton* go = [NSButton buttonWithTitle:@"Go" target:self action:@selector(goPressed:)];
    go.controlSize = NSControlSizeSmall;
    NSView* spacer = [[NSView alloc] initWithFrame:NSZeroRect];
    [spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ label, _address, go, spacer ]];
    NSScrollView* scroll = nil;
    _view = [GRPaneUtil textViewWithScroll:&scroll];
    [GRPaneUtil stack:@[ bar, scroll ] in:root grow:scroll];
    self.view = root;
    [self show:@"Memory is available while the program is paused."];
}

- (void)show:(NSString*)text
{
    GRTheme* t = [[GRTheme current] resolvedFor:_view.effectiveAppearance];
    _view.backgroundColor = t.window;
    NSDictionary* attrs = @{ NSFontAttributeName: [GRPaneUtil monoFont], NSForegroundColorAttributeName: t.editorText };
    [_view.textStorage setAttributedString:[[NSAttributedString alloc] initWithString:text attributes:attrs]];
}

- (void)refreshPaused:(BOOL)paused locals:(const std::vector<Services::DebugVariable>&)locals backend:(NSString*)backend
{
    if (!self.isViewLoaded) [self loadView];
    _paused = paused;
    _locals = locals;
    _backend = [backend copy];
    [self update];
}

- (void)update
{
    if (!_paused) { [self show:@"Memory is available while the program is paused."]; return; }
    // nothing typed yet: where the stop's first variable lives
    if (!_chosen)
        for (const Services::DebugVariable& v : _locals)
            if (Core::StringUtil::StartsWith(v.Address, "0x")) { _address.stringValue = GRNS(v.Address); break; }
    NSString* typed = [_address.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (typed.length == 0) { [self show:@"Type an address and press Return."]; return; }
    std::string a = Core::StringUtil::Replace(Core::StringUtil::ToLower(GRStd(typed)), "`", "");
    unsigned long long addr = 0;
    if (!Core::StringUtil::TryParseHex(a, addr)) { [self show:@"Invalid address."]; return; }
    NSData* data = self.readMemory ? self.readMemory(addr, 256) : nil;
    if (data.length == 0)
    {
        [self show:[NSString stringWithFormat:@"(no memory at 0x%llx from %@)", addr, GROr(_backend, @"the debugger")]];
        return;
    }
    const unsigned char* b = (const unsigned char*)data.bytes;
    NSUInteger n = data.length;
    BOOL wide = addr + n > 0xFFFFFFFFull;
    NSMutableString* sb = [NSMutableString string];
    for (NSUInteger row = 0; row < n; row += 16)
    {
        if (wide) [sb appendFormat:@"%016llX  ", addr + row];
        else [sb appendFormat:@"%08llX  ", addr + row];
        NSMutableString* ascii = [NSMutableString string];
        for (NSUInteger i = 0; i < 16 && row + i < n; i++)
        {
            unsigned char v = b[row + i];
            [sb appendFormat:@"%02X ", v];
            [ascii appendFormat:@"%c", (v >= 32 && v < 127) ? (char)v : '.'];
        }
        [sb appendString:@" "];
        [sb appendString:ascii];
        [sb appendString:@"\n"];
    }
    [self show:sb];
}

- (void)goPressed:(id)sender
{
    (void)sender;
    _chosen = [_address.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]].length > 0;
    [self update];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (control != _address || commandSelector != @selector(insertNewline:)) return NO;
    [self goPressed:nil];
    return YES;
}

- (void)clearView
{
    _paused = NO;
    _locals.clear();
    if (self.isViewLoaded) [self show:@""];
}
@end
