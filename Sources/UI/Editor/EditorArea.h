// =============================================================================
//  EditorArea.h - GREditorArea: the tabbed document area of the main window
//  (GRIDEd's UI/Editor/DocumentManager.h)
//
//    +--------------------------------------------------------------+
//    | main.c | kalman.c x | guide.shl                         v    |  GRTabBar
//    | NavFilter > src > kalman.c > kalman_update()                 |  jump bar (NSPathControl)
//    | 12  o   x = F * x;                                           |  GRCodeView + GRGutter
//    | 13  >   P = F * P * Ft + Q;                                  |  (two of them, split)
//    +--------------------------------------------------------------+
//
//  Opens, switches and closes GRSourceDocuments (asking to save a modified
//  one), keeps each document's scroll position, and splits the editor into two
//  views of the same text. The active document becomes the window
//  controller's document, so Save, Save As, Revert, Print and Undo reach it
//  through the responder chain.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../../Core/LanguageTarget.h"

@class GRSourceDocument;
@class GRCodeView;
@class GREditorArea;

@protocol GREditorAreaDelegate <NSObject>
@optional
- (void)editorArea:(GREditorArea*)area didActivate:(GRSourceDocument*)document;     // nil: none left
- (void)editorAreaDocumentsChanged:(GREditorArea*)area;                             // opened, closed, renamed, modified
- (void)editorArea:(GREditorArea*)area caretAtLine:(NSInteger)line column:(NSInteger)column;
- (void)editorArea:(GREditorArea*)area breakpointToggled:(GRSourceDocument*)document line:(NSInteger)line;
- (void)editorArea:(GREditorArea*)area jumpToPath:(NSString*)path;                  // a jump bar folder chosen
@end

@interface GREditorArea : NSView

@property (nonatomic, weak) id<GREditorAreaDelegate> delegate;
// the main window's controller: the active document is attached to it
@property (nonatomic, weak) NSWindowController* windowController;
// the jump bar starts at the project: "NavFilter > src > kalman.c"
@property (nonatomic, copy) NSString* projectName;
@property (nonatomic, copy) NSString* projectRoot;
// the language a new file and a header follow
@property (nonatomic, assign) GRIDEm::Core::Language projectLanguage;

@property (nonatomic, readonly) NSArray<GRSourceDocument*>* documents;   // in tab order
@property (nonatomic, readonly) GRSourceDocument* activeDocument;
// the view with the keyboard (the lower pane when split and focused there)
@property (nonatomic, readonly) GRCodeView* activeCodeView;
@property (nonatomic, readonly) BOOL isSplit;
@property (nonatomic, readonly) NSArray<GRSourceDocument*>* modifiedDocuments;

// ---- opening ---------------------------------------------------------------------
// opens a file, or brings it forward when it is open already; nil with error
- (GRSourceDocument*)openPath:(NSString*)path error:(NSError**)error;
- (GRSourceDocument*)openPath:(NSString*)path line:(NSInteger)line column:(NSInteger)column error:(NSError**)error;
- (GRSourceDocument*)newUntitled:(GRIDEm::Core::Language)language;
- (GRSourceDocument*)documentForPath:(NSString*)path;                    // nil when not open
- (void)activateDocument:(GRSourceDocument*)document;
- (void)selectNext:(BOOL)forward;                                         // ⌃Tab / ⌃⇧Tab

// ---- closing (a modified document asks first; done is told whether all closed) -----
- (void)closeDocument:(GRSourceDocument*)document done:(void (^)(BOOL closed))done;
- (void)closeActive;
- (void)closeAll:(void (^)(BOOL closed))done;
- (void)closeAllBut:(GRSourceDocument*)keep done:(void (^)(BOOL closed))done;
// closes at once, never asking (the workspace closed after its own question)
- (void)discardAll;

// ---- saving --------------------------------------------------------------------------
- (BOOL)saveAll:(NSError**)error;                                          // the untitled ones are left
- (void)saveDocument:(GRSourceDocument*)document;                         // the panel for an untitled one

// ---- the views -----------------------------------------------------------------------
- (void)toggleSplit;
- (void)togglePin:(GRSourceDocument*)document;
- (void)applySettings;                                                     // font, tabs, theme, line numbers
- (void)refreshTabs;
// the debugger's line in a file (0 removes it from every view)
- (void)showExecutionLine:(NSInteger)line inPath:(NSString*)path;
- (void)refreshGutters;                                                   // breakpoints or marks changed
- (NSArray<GRCodeView*>*)codeViewsFor:(GRSourceDocument*)document;
- (void)focusEditor;
@end
