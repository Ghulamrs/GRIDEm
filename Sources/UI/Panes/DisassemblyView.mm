// =============================================================================
//  DisassemblyView.mm - see DisassemblyView.h
// =============================================================================
#import "DisassemblyView.h"
#import "../Common.h"
#import "PaneUtil.h"
#import "../Theme.h"
#include "../../Core/TextFile.h"
#include "../../Platform/Paths.h"

using namespace GRIDEm;

@implementation GRDisassemblyView
{
    NSTextField* _header;
    NSTextView* _view;
    NSArray<NSString*>* _lines;
    NSInteger _current;
}

- (instancetype)init
{
    return [super initWithIdentifier:@"Disassembly" title:@"Disassembly" symbol:@"chevron.left.forwardslash.chevron.right"];
}

- (void)loadView
{
    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200)];
    _header = [GRPaneUtil label:@""];
    _header.textColor = [NSColor secondaryLabelColor];
    NSStackView* bar = [GRPaneUtil barWithViews:@[ _header ]];
    NSScrollView* scroll = nil;
    _view = [GRPaneUtil textViewWithScroll:&scroll];
    [GRPaneUtil stack:@[ bar, scroll ] in:root grow:scroll];
    self.view = root;
}

- (void)showText:(NSString*)text
{
    _lines = [text componentsSeparatedByString:@"\n"];
    [self redraw];
}

- (void)showFile:(NSString*)source assembly:(NSString*)asmPath
{
    if (!self.isViewLoaded) [self loadView];
    std::string a8 = GRStd(asmPath);
    if (asmPath.length == 0 || !Platform::Paths::IsFile(a8))
    {
        _header.stringValue = source != nil ? source.lastPathComponent : @"";
        [self showText:[NSString stringWithFormat:@"No generated assembly found yet for this file.\n"
                                                  "Build the project (⌘B) - the compiler writes it to:\n  %@",
                                                  GROr(asmPath, @"")]];
        return;
    }
    _header.stringValue = asmPath;
    std::string text, error;
    Core::FileEncoding enc;
    if (!Core::TextFile::Read(a8, text, enc, error)) { [self showText:GRNS(error)]; return; }
    [self showText:GRNS(text)];
}

- (void)setCurrentLine:(NSInteger)line
{
    if (!self.isViewLoaded) [self loadView];
    _current = line;
    [self redraw];
}

- (void)redraw
{
    if (!self.isViewLoaded) return;
    GRTheme* t = [[GRTheme current] resolvedFor:_view.effectiveAppearance];
    _view.backgroundColor = t.window;
    NSMutableAttributedString* all = [[NSMutableAttributedString alloc] init];
    NSFont* font = [GRPaneUtil monoFont];
    NSDictionary* plain = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: t.editorText };
    NSDictionary* exec = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: t.editorText,
                            NSBackgroundColorAttributeName: t.execLine };
    NSUInteger currentStart = 0;
    for (NSUInteger i = 0; i < _lines.count; i++)
    {
        BOOL here = (NSInteger)i + 1 == _current;
        if (here) currentStart = all.length;
        NSString* row = [NSString stringWithFormat:@"%@ %@\n", here ? @"➤" : @" ", _lines[i]];
        [all appendAttributedString:[[NSAttributedString alloc] initWithString:row attributes:here ? exec : plain]];
    }
    [_view.textStorage setAttributedString:all];
    if (_current > 0) [_view scrollRangeToVisible:NSMakeRange(currentStart, 0)];
}

- (void)clearView
{
    _current = 0;
    [self redraw];
}

- (void)applyTheme
{
    [super applyTheme];
    [self redraw];
}
@end
