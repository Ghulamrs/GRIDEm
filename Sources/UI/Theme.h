// =============================================================================
//  Theme.h - GRTheme: System / Dark / Light / Blue (GRIDEd's UI/Theme.h)
//  GRIDEd paints every control itself; on the Mac the window chrome is the
//  system's (NSAppearance), and a theme gives the editor, the gutter and the
//  panes' text their colours. "System" follows the Mac's light or dark mode;
//  Dark, Light and Blue are GRIDEd's three, chosen per window.
//  The user's syntax colours (gride.settings.json "syntaxColors") go over it.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../Settings/GrideSettings.h"

typedef NS_ENUM(NSInteger, GRTokenKind) {
    GRTokenPlain = 0,
    GRTokenKeyword,
    GRTokenType,
    GRTokenString,
    GRTokenNumber,
    GRTokenComment,
    GRTokenPreprocessor,
    GRTokenFunction
};

// posted (object: the GRTheme) when the current theme or its colours change
extern NSString* const GRThemeDidChangeNotification;

@interface GRTheme : NSObject <NSCopying>

@property (nonatomic, copy) NSString* name;
// chrome (used by panes GRIDEm draws itself: tab bar, gutter, status line)
@property (nonatomic, strong) NSColor* window;      // editor / document background
@property (nonatomic, strong) NSColor* panel;
@property (nonatomic, strong) NSColor* header;
@property (nonatomic, strong) NSColor* border;
@property (nonatomic, strong) NSColor* text;
@property (nonatomic, strong) NSColor* mutedText;
@property (nonatomic, strong) NSColor* accent;
@property (nonatomic, strong) NSColor* selection;
@property (nonatomic, strong) NSColor* hover;
// editor
@property (nonatomic, strong) NSColor* editorText;
@property (nonatomic, strong) NSColor* lineNumber;
@property (nonatomic, strong) NSColor* currentLine;
@property (nonatomic, strong) NSColor* execLine;     // the debugger's current statement
@property (nonatomic, strong) NSColor* breakpoint;
@property (nonatomic, strong) NSColor* bookmark;
@property (nonatomic, strong) NSColor* braceMatch;
// syntax
@property (nonatomic, strong) NSColor* keyword;
@property (nonatomic, strong) NSColor* typeName;
@property (nonatomic, strong) NSColor* stringLit;
@property (nonatomic, strong) NSColor* number;
@property (nonatomic, strong) NSColor* comment;
@property (nonatomic, strong) NSColor* preprocessor;
@property (nonatomic, strong) NSColor* function;
// output
@property (nonatomic, strong) NSColor* errorText;
@property (nonatomic, strong) NSColor* warningText;
@property (nonatomic, strong) NSColor* successText;

@property (nonatomic, readonly) BOOL isDark;
@property (nonatomic, readonly) BOOL followsSystem;  // "System"

+ (GRTheme*)dark;
+ (GRTheme*)light;
+ (GRTheme*)blue;
+ (GRTheme*)system;                                   // Dark or Light colours by the appearance
+ (GRTheme*)themeNamed:(NSString*)name;               // unknown names give System
+ (NSArray<NSString*>*)names;                         // System, Dark, Light, Blue
+ (NSArray<NSString*>*)tokenNames;                    // text, keyword, type, string, number, comment, preprocessor, function

// the theme in force for the app; setting it posts GRThemeDidChangeNotification
+ (GRTheme*)current;
+ (void)setCurrent:(GRTheme*)theme;

// the colours to paint with under an appearance: System resolves to Dark or Light
- (GRTheme*)resolvedFor:(NSAppearance*)appearance;
// the NSAppearance a window takes (nil for System)
- (NSAppearance*)appearance;
- (void)applyToWindow:(NSWindow*)window;

- (NSColor*)colorForToken:(GRTokenKind)kind;
- (NSColor*)colorForTokenName:(NSString*)token;
- (void)setColor:(NSColor*)color forTokenName:(NSString*)token;
// the user's overrides, token -> "#RRGGBB"
- (void)applyOverrides:(const GRIDEm::Settings::OrderedStringMap&)overrides;

+ (NSColor*)colorFromHex:(NSString*)hex;              // nil when it is not "#RRGGBB"
+ (NSString*)hexFromColor:(NSColor*)color;
@end
