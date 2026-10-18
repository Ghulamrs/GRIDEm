// =============================================================================
//  CodeView.h - GRCodeView: the source editor (GRIDEd's UI/Editor/CodeEditor.h
//  and CodeTextBox.h) as an NSTextView on TextKit 1 - an NSLayoutManager, so the
//  colours are temporary attributes and the gutter is an NSRulerView.
//
//  What GRIDEd's editor does, it does: auto-indent after Enter (one more level
//  after "{"), Tab / Shift+Tab indenting a selection of lines, spaces or tabs,
//  comment / uncomment, upper / lower case, tabify / untabify, format by brace
//  depth, go to line, bookmarks, the current line shaded, the matching brace
//  marked, the debugger's line shaded, zoom, white space shown.
//  Several views can show one GRSourceDocument (the split editor): each has a
//  layout manager of its own over the document's text storage.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#import "../Theme.h"
#include "../../Settings/GrideSettings.h"

@class GRSourceDocument;
@class GRHighlighter;
@class GRGutter;

@interface GRCodeView : NSTextView

// a scroll view holding a new code view over the document's text storage, with its gutter
+ (NSScrollView*)scrollViewForDocument:(GRSourceDocument*)document;
// the code view inside such a scroll view
+ (GRCodeView*)codeViewIn:(NSScrollView*)scrollView;

@property (nonatomic, weak, readonly) GRSourceDocument* document;
@property (nonatomic, readonly) GRHighlighter* highlighter;
@property (nonatomic, readonly) GRGutter* gutter;
@property (nonatomic, strong) GRTheme* theme;              // resolved colours
@property (nonatomic, assign) int tabSize;
@property (nonatomic, assign) BOOL insertSpaces;
@property (nonatomic, assign) BOOL autoIndent;
@property (nonatomic, assign) CGFloat fontSize;
@property (nonatomic, copy) NSString* fontName;
@property (nonatomic, assign) BOOL showWhitespace;
@property (nonatomic, assign) NSInteger executionLine;     // the debugger's line, 0 for none

// takes this view off the document's text storage (before the view goes)
- (void)detachFromDocument;

// ---- settings ------------------------------------------------------------------
- (void)applyEnvironment:(GRIDEm::Settings::EnvironmentSettingsPtr)env;
- (void)applyTheme:(GRTheme*)theme;
// the language again (after Save As or a change of project language)
- (void)applyLanguage;
- (void)zoomBy:(CGFloat)delta;
- (void)resetZoom:(CGFloat)size;

// ---- where the caret is (1-based) ---------------------------------------------
- (NSInteger)currentLine;
- (NSInteger)currentColumn;
- (NSInteger)lineCount;
- (NSRange)rangeOfLine:(NSInteger)line;                    // without its line end
- (NSInteger)lineOfCharacter:(NSUInteger)index;
- (void)goToLine:(NSInteger)line;
- (void)goToLine:(NSInteger)line column:(NSInteger)column;
- (void)selectRange:(NSRange)range;

// ---- editing (each one undo step) ----------------------------------------------
- (void)commentSelection:(BOOL)comment;
- (void)indentSelection:(BOOL)indent;
- (void)changeCase:(BOOL)upper;
- (void)convertIndentation:(BOOL)toTabs;
- (void)tabify;                                             // to tabs when tabs are used, else to spaces
- (void)formatDocument;
- (void)insertSnippet:(NSString*)text;
- (NSString*)indentUnit;

// ---- bookmarks and breakpoints (kept in the document) -----------------------------
- (void)toggleBookmark;
- (void)nextBookmark:(BOOL)forward;
- (void)clearBookmarks;
// line 0: the caret's line. breakpointToggled is told the line afterwards
- (void)toggleBreakpoint:(NSInteger)line;
@property (nonatomic, copy) void (^breakpointToggled)(NSInteger line);
// the caret moved or the text changed: the status line's "Ln 13, Col 5"
@property (nonatomic, copy) void (^caretMoved)(NSInteger line, NSInteger column);
@end
