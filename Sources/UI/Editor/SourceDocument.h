// =============================================================================
//  SourceDocument.h - GRSourceDocument: one open file (GRIDEd's
//  UI/Editor/EditorDocument.h), as an NSDocument, so the Mac's autosave,
//  Versions, "changed on disk" and the Save panel come with it.
//
//  The bytes are read and written through Core::TextFile, so a file keeps its
//  encoding (UTF-8, UTF-8 BOM, UTF-16 LE/BE, Windows-1252) and its line endings
//  (CRLF stays CRLF - a file made on Windows goes back as it came). In memory
//  the text has "\n" line ends; the line end is put back on saving.
//
//  The document owns its text storage: every editor view showing it (the
//  split editor's two panes) lays out the same NSTextStorage.
//  It has no window of its own - GREditorArea shows it in the main window.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../../Core/LanguageTarget.h"
#include "../../Core/TextFile.h"
#include "../../Settings/GrideSettings.h"
#include <memory>
#include <set>

// posted (object: the document) when it is modified or saved, renamed, made read-only or pinned:
// the tab bar and the window title follow it
extern NSString* const GRSourceDocumentStateNotification;

@interface GRSourceDocument : NSDocument

// the settings the app runs with (language detection, tab size); set once at start
+ (void)setSettings:(GRIDEm::Settings::GrideSettingsPtr)settings;
+ (GRIDEm::Settings::GrideSettingsPtr)settings;
// asked before a document is written to its file; NO cancels the save (GRSession mayWriteFile:)
+ (void)setWriteGuard:(BOOL (^)(NSString* path))guard;

// a new, unsaved document, "Untitled1.c" ... in the language given
+ (GRSourceDocument*)untitledWithLanguage:(GRIDEm::Core::Language)language;
// opens a file (not added to NSDocumentController); nil with error when it cannot be read
+ (GRSourceDocument*)openPath:(NSString*)path error:(NSError**)error;

@property (nonatomic, readonly) NSTextStorage* textStorage;
@property (nonatomic, copy) NSString* text;                       // "\n" line ends
@property (nonatomic, readonly) NSString* path;                   // nil while untitled
// displayName (NSDocument's): the file name, or "Untitled1.c" while untitled
@property (nonatomic, assign) GRIDEm::Core::FileEncoding fileEncoding;
@property (nonatomic, copy) NSString* lineEnding;                 // "CRLF" or "LF"
@property (nonatomic, readonly) NSString* encodingName;           // "UTF-8", "UTF-16 LE" ...
@property (nonatomic, assign) GRIDEm::Core::Language language;
@property (nonatomic, assign) GRIDEm::Core::Language projectLanguage;   // what a .h follows
@property (nonatomic, assign) BOOL readOnly;                      // held read-only: Workspace::IsDocumentReadOnly
@property (nonatomic, assign) BOOL pinned;                        // a pinned tab
@property (nonatomic, readonly) BOOL isModified;

// the breakpoints of the file, 1-based lines; the IDE keeps one set per file and shares it
// with the editor (and the workspace and the debugger) - GRIDEd's ShareBreakpoints
@property (nonatomic, assign) std::shared_ptr<std::set<int> > breakpoints;
@property (nonatomic, assign) std::set<int> bookmarks;

// the language again from the file name (after Save As)
- (void)detectLanguage;
// the text as it goes to disk (line ends put back)
- (NSString*)textForDisk;
// re-reads the file (changed on disk); NO with error when it cannot be read
- (BOOL)reload:(NSError**)error;
// saves at once, without a panel when it has a path; NO with error
- (BOOL)saveNow:(NSError**)error;
@end
