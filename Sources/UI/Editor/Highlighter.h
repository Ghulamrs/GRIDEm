// =============================================================================
//  Highlighter.h - GRHighlighter: splits source text into coloured tokens and
//  paints them on an NSTextView (GRIDEd's UI/Editor/SyntaxHighlighter.h)
//
//  Driven entirely by LanguageSettings (keywords, types, comment markers), so
//  C90, C++11, Shalimar, assembly and linker files use the same code. Colours
//  are temporary attributes on the layout manager, so colouring never enters
//  the undo history and never marks the document edited. Only the visible part
//  is coloured, again after each edit and each scroll, as GRIDEd does it.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#import "../Theme.h"
#include "../../Settings/GrideSettings.h"
#include <vector>

struct GRToken
{
    NSUInteger start;
    NSUInteger length;
    GRTokenKind kind;
};

@interface GRHighlighter : NSObject

// lang null: plain text, nothing coloured
- (instancetype)initWithLanguage:(GRIDEm::Settings::LanguageSettingsPtr)lang;
- (void)setLanguage:(GRIDEm::Settings::LanguageSettingsPtr)lang;
@property (nonatomic, readonly) BOOL isPlain;
@property (nonatomic, strong) GRTheme* theme;          // resolved colours (never "System")

// watches the view's text and scrolling and keeps the visible part coloured
- (void)attachToTextView:(NSTextView*)view;
- (void)detach;
- (void)highlightVisible;
- (void)highlightAll;                                    // the whole text (printing, a small file)

// true when position pos lies inside a block comment
- (BOOL)inBlockComment:(NSString*)text from:(NSUInteger)from pos:(NSUInteger)pos;
// tokens in text[start, end); startInComment is the state at start
- (std::vector<GRToken>)tokenize:(NSString*)text start:(NSUInteger)start end:(NSUInteger)end
                  startInComment:(BOOL)startInComment;
@end
