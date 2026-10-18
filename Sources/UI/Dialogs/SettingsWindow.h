// =============================================================================
//  SettingsWindow.h - GRIDEm > Settings… ⌘, (GRIDEd's UI/Dialogs/OptionsDialog.h)
//
//  A toolbar of pages (NSTabViewController, toolbar style), editing a clone of
//  the settings; OK commits every page (Definitions last: its reload rebases
//  the toolchain pages' untouched values), saves gride.settings.json through
//  SettingsManager and hands the result to the applied block. Cancel drops it.
//
//    General           theme, start-up and build, project folder, help folder
//    Editor            tabs, spaces, line numbers, white space
//    Fonts & Colours   editor font and size, syntax colours over the theme
//    Keyboard          key set, and a shortcut per command
//    Toolchains        per target: tools (found / not installed), search paths,
//                      command templates, flag spellings, tool environment
//    Definitions       toolchain.json: the user's overrides per tool / library
//    RIDE Paths        RIDE's root and headers, TI's directories, shared folders,
//                      the default compiler, the C6747 debugger
//    Converters        C <-> Shalimar
//    CCS               Code Composer Studio projects
//    Languages         extensions, comments, keywords and types per language
//    Compiler Options  RIDE's Compiler Options a new project starts with
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../../Settings/GrideSettings.h"

typedef void (^GRSettingsApplied)(const GRIDEm::Settings::GrideSettingsPtr& result);

@class GRSettingsWindow;

// ---- one page ------------------------------------------------------------------
@interface GRSettingsPage : NSViewController
@property (nonatomic, readonly) NSString* pageId;          // "General", "Keyboard" ...
@property (nonatomic, readonly) NSString* symbolName;
@property (nonatomic, readonly) GRIDEm::Settings::GrideSettingsPtr work;
@property (nonatomic, weak) GRSettingsWindow* owner;
- (instancetype)initWithId:(NSString*)pageId title:(NSString*)title symbol:(NSString*)symbol
                  settings:(const GRIDEm::Settings::GrideSettingsPtr&)work;
// subclasses: the page's content (above the shared OK / Cancel bar)
- (NSView*)makeContent;
// subclasses: the controls into the clone
- (void)commit;
@end

@interface GRGeneralPage : GRSettingsPage @end
@interface GREditorPage : GRSettingsPage @end
@interface GRFontsColoursPage : GRSettingsPage @end
@interface GRKeyboardPage : GRSettingsPage @end          // KeyboardPage.mm
@interface GRToolchainsPage : GRSettingsPage @end        // ToolchainsPage.mm
@interface GRDefinitionsPage : GRSettingsPage @end       // DefinitionsPage.mm
@interface GRRidePathsPage : GRSettingsPage @end
@interface GRConvertersPage : GRSettingsPage @end
@interface GRCcsPage : GRSettingsPage @end
@interface GRLanguagesPage : GRSettingsPage @end
@interface GRCompilerOptionsPage : GRSettingsPage @end

// ---- the window ----------------------------------------------------------------
@interface GRSettingsWindow : NSWindowController
// shows the window on startPage (nil: General); one window at a time
+ (void)showWithSettings:(const GRIDEm::Settings::GrideSettingsPtr&)current
               startPage:(NSString*)startPage
                 applied:(GRSettingsApplied)applied;
- (void)okPressed:(id)sender;
- (void)cancelPressed:(id)sender;
@end
