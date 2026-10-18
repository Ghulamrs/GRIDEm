// =============================================================================
//  Gutter.h - GRGutter: the strip left of the editor (GRIDEd's LineGutter, in
//  UI/Editor/CodeEditor.h), as the scroll view's vertical NSRulerView.
//    line numbers                  (View > Line Numbers)
//    breakpoints                   click a line to set or clear one; drag one off to delete it
//    the current-line arrow        the debugger's stop
//    bookmarks                     a small mark at the right edge
//    error / warning marks         where the last build complained
//  Breakpoints and bookmarks live in the document (GRSourceDocument), so every
//  view of it - and the workspace and the debugger - see the same set.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include <map>

@class GRCodeView;

typedef NS_ENUM(NSInteger, GRMarkKind) {
    GRMarkWarning = 1,
    GRMarkError = 2
};

@interface GRGutter : NSRulerView

- (instancetype)initWithCodeView:(GRCodeView*)codeView;

@property (nonatomic, weak, readonly) GRCodeView* codeView;
@property (nonatomic, assign) BOOL showNumbers;
// line -> the worst mark on it, from the last build
@property (nonatomic, assign) std::map<int, GRMarkKind> marks;
// the message shown when the pointer rests on a marked line
@property (nonatomic, strong) NSDictionary<NSNumber*, NSString*>* markMessages;

- (void)clearMarks;
- (void)updateWidth;           // after the line count or the font changed
@end
