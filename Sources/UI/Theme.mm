// =============================================================================
//  Theme.mm - see Theme.h. The colours are GRIDEd's, value for value.
// =============================================================================
#import "Theme.h"

NSString* const GRThemeDidChangeNotification = @"GRThemeDidChangeNotification";

static GRTheme* gCurrent = nil;

@implementation GRTheme
{
    BOOL _followsSystem;
}

+ (NSColor*)colorFromHex:(NSString*)hex
{
    NSString* h = [hex stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([h hasPrefix:@"#"]) h = [h substringFromIndex:1];
    if (h.length != 6) return nil;
    unsigned int v = 0;
    NSScanner* s = [NSScanner scannerWithString:h];
    if (![s scanHexInt:&v] || !s.isAtEnd) return nil;
    return [NSColor colorWithSRGBRed:((v >> 16) & 0xFF) / 255.0 green:((v >> 8) & 0xFF) / 255.0
                                blue:(v & 0xFF) / 255.0 alpha:1.0];
}

+ (NSString*)hexFromColor:(NSColor*)color
{
    NSColor* c = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (c == nil) return @"#000000";
    int r = (int)lround(c.redComponent * 255), g = (int)lround(c.greenComponent * 255), b = (int)lround(c.blueComponent * 255);
    return [NSString stringWithFormat:@"#%02X%02X%02X", r, g, b];
}

static NSColor* Hex(NSString* h) { return [GRTheme colorFromHex:h]; }

+ (GRTheme*)dark
{
    GRTheme* t = [[GRTheme alloc] init];
    t.name = @"Dark";
    t.window = Hex(@"#17191C"); t.panel = Hex(@"#1F2226"); t.header = Hex(@"#26292E");
    t.border = Hex(@"#33373D"); t.text = Hex(@"#D6D9DE"); t.mutedText = Hex(@"#9AA1AB");
    t.accent = Hex(@"#E0A23B"); t.selection = Hex(@"#2C3A4A"); t.hover = Hex(@"#33373D");
    t.editorText = Hex(@"#D6D9DE"); t.lineNumber = Hex(@"#5F666F"); t.currentLine = Hex(@"#222A33");
    t.execLine = Hex(@"#4A4020"); t.breakpoint = Hex(@"#E06C5B"); t.bookmark = Hex(@"#4FB3BF");
    t.braceMatch = Hex(@"#3B4A5C");
    t.keyword = Hex(@"#6FA8DC"); t.typeName = Hex(@"#5FC1A8"); t.stringLit = Hex(@"#D9A066"); t.number = Hex(@"#B5CEA8");
    t.comment = Hex(@"#8A9A86"); t.preprocessor = Hex(@"#C49BD8"); t.function = Hex(@"#E6DB8A");
    t.errorText = Hex(@"#F08A7E"); t.warningText = Hex(@"#E8C27A"); t.successText = Hex(@"#8FD5B4");
    return t;
}

+ (GRTheme*)light
{
    GRTheme* t = [[GRTheme alloc] init];
    t.name = @"Light";
    t.window = Hex(@"#FFFFFF"); t.panel = Hex(@"#F3F3F5"); t.header = Hex(@"#E2E4E8");
    t.border = Hex(@"#CDD0D5"); t.text = Hex(@"#1E1F22"); t.mutedText = Hex(@"#5D636B");
    t.accent = Hex(@"#D08A1A"); t.selection = Hex(@"#CFE1F5"); t.hover = Hex(@"#DDE1E6");
    t.editorText = Hex(@"#1E1F22"); t.lineNumber = Hex(@"#8A9099"); t.currentLine = Hex(@"#F1F5FB");
    t.execLine = Hex(@"#FFF2B3"); t.breakpoint = Hex(@"#C8392B"); t.bookmark = Hex(@"#1F7F8C");
    t.braceMatch = Hex(@"#D9E4F2");
    t.keyword = Hex(@"#0B57A4"); t.typeName = Hex(@"#127E6B"); t.stringLit = Hex(@"#A1490F"); t.number = Hex(@"#2F7D32");
    t.comment = Hex(@"#5F7A5B"); t.preprocessor = Hex(@"#7A3E9D"); t.function = Hex(@"#6F5A00");
    t.errorText = Hex(@"#B3261E"); t.warningText = Hex(@"#8A5A00"); t.successText = Hex(@"#1E7A46");
    return t;
}

+ (GRTheme*)blue
{
    GRTheme* t = [self light];
    t.name = @"Blue";
    t.panel = Hex(@"#DCE2EE"); t.header = Hex(@"#C2CCE0");
    t.border = Hex(@"#8E9BBC"); t.accent = Hex(@"#3D6CC0");
    t.selection = Hex(@"#B7CBEE"); t.hover = Hex(@"#CCD7EC");
    return t;
}

+ (GRTheme*)system
{
    GRTheme* t = [self light];
    t.name = @"System";
    t->_followsSystem = YES;
    return t;
}

+ (NSArray<NSString*>*)names { return @[ @"System", @"Dark", @"Light", @"Blue" ]; }

+ (NSArray<NSString*>*)tokenNames
{
    return @[ @"text", @"keyword", @"type", @"string", @"number", @"comment", @"preprocessor", @"function" ];
}

+ (GRTheme*)themeNamed:(NSString*)name
{
    if ([name caseInsensitiveCompare:@"Dark"] == NSOrderedSame) return [self dark];
    if ([name caseInsensitiveCompare:@"Light"] == NSOrderedSame) return [self light];
    if ([name caseInsensitiveCompare:@"Blue"] == NSOrderedSame) return [self blue];
    return [self system];
}

+ (GRTheme*)current
{
    if (gCurrent == nil) gCurrent = [self system];
    return gCurrent;
}

+ (void)setCurrent:(GRTheme*)theme
{
    gCurrent = theme;
    [[NSNotificationCenter defaultCenter] postNotificationName:GRThemeDidChangeNotification object:theme];
}

- (id)copyWithZone:(NSZone*)zone
{
    GRTheme* t = [[GRTheme allocWithZone:zone] init];
    t.name = self.name;
    t->_followsSystem = _followsSystem;
    t.window = self.window; t.panel = self.panel; t.header = self.header; t.border = self.border;
    t.text = self.text; t.mutedText = self.mutedText; t.accent = self.accent; t.selection = self.selection;
    t.hover = self.hover; t.editorText = self.editorText; t.lineNumber = self.lineNumber;
    t.currentLine = self.currentLine; t.execLine = self.execLine; t.breakpoint = self.breakpoint;
    t.bookmark = self.bookmark; t.braceMatch = self.braceMatch; t.keyword = self.keyword;
    t.typeName = self.typeName; t.stringLit = self.stringLit; t.number = self.number; t.comment = self.comment;
    t.preprocessor = self.preprocessor; t.function = self.function; t.errorText = self.errorText;
    t.warningText = self.warningText; t.successText = self.successText;
    return t;
}

- (BOOL)followsSystem { return _followsSystem; }

- (BOOL)isDark
{
    NSColor* c = [self.window colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (c == nil) return NO;
    double brightness = 0.299 * c.redComponent + 0.587 * c.greenComponent + 0.114 * c.blueComponent;
    return brightness < 0.5;
}

// the overrides a System theme was given are kept by applying them again on the resolved copy
- (GRTheme*)resolvedFor:(NSAppearance*)appearance
{
    if (!_followsSystem) return self;
    NSAppearanceName best = [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    BOOL dark = [best isEqualToString:NSAppearanceNameDarkAqua];
    GRTheme* base = dark ? [GRTheme dark] : [GRTheme light];
    // a System theme carries the user's syntax colours on top of Light; take those over
    GRTheme* plainLight = [GRTheme light];
    for (NSString* token in [GRTheme tokenNames])
    {
        NSColor* mine = [self colorForTokenName:token];
        NSColor* stock = [plainLight colorForTokenName:token];
        if (![[GRTheme hexFromColor:mine] isEqualToString:[GRTheme hexFromColor:stock]]) [base setColor:mine forTokenName:token];
    }
    base.name = @"System";
    return base;
}

- (NSAppearance*)appearance
{
    if (_followsSystem) return nil;
    return [NSAppearance appearanceNamed:self.isDark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

- (void)applyToWindow:(NSWindow*)window
{
    window.appearance = [self appearance];
}

- (NSColor*)colorForToken:(GRTokenKind)kind
{
    switch (kind)
    {
    case GRTokenKeyword:      return self.keyword;
    case GRTokenType:         return self.typeName;
    case GRTokenString:       return self.stringLit;
    case GRTokenNumber:       return self.number;
    case GRTokenComment:      return self.comment;
    case GRTokenPreprocessor: return self.preprocessor;
    case GRTokenFunction:     return self.function;
    default:                  return self.editorText;
    }
}

- (NSColor*)colorForTokenName:(NSString*)token
{
    if ([token isEqualToString:@"keyword"]) return self.keyword;
    if ([token isEqualToString:@"type"]) return self.typeName;
    if ([token isEqualToString:@"string"]) return self.stringLit;
    if ([token isEqualToString:@"number"]) return self.number;
    if ([token isEqualToString:@"comment"]) return self.comment;
    if ([token isEqualToString:@"preprocessor"]) return self.preprocessor;
    if ([token isEqualToString:@"function"]) return self.function;
    return self.editorText;
}

- (void)setColor:(NSColor*)color forTokenName:(NSString*)token
{
    if (color == nil) return;
    if ([token isEqualToString:@"keyword"]) self.keyword = color;
    else if ([token isEqualToString:@"type"]) self.typeName = color;
    else if ([token isEqualToString:@"string"]) self.stringLit = color;
    else if ([token isEqualToString:@"number"]) self.number = color;
    else if ([token isEqualToString:@"comment"]) self.comment = color;
    else if ([token isEqualToString:@"preprocessor"]) self.preprocessor = color;
    else if ([token isEqualToString:@"function"]) self.function = color;
    else self.editorText = color;
}

- (void)applyOverrides:(const GRIDEm::Settings::OrderedStringMap&)overrides
{
    for (const std::pair<std::string, std::string>& kv : overrides.Items())
    {
        if (kv.second.empty()) continue;
        NSColor* c = [GRTheme colorFromHex:[NSString stringWithUTF8String:kv.second.c_str()]];
        if (c != nil) [self setColor:c forTokenName:[NSString stringWithUTF8String:kv.first.c_str()]];
    }
}
@end
