// =============================================================================
//  Commands.h - every menu item and toolbar button is a GRCommand
//  (GRIDEd's UI/Commands.h - Command, CommandRegistry, KeyText, KeySets - and
//  the table of MainForm.Commands.cpp's RegisterCommands / BuildMenus)
//
//  GRCommand          id (GRIDEd's own, "Build.BuildProject"), title, menu group,
//                     SF Symbol, shortcut, handler, enabled / checked tests
//  GRCommandRegistry  the one table; builds the main menu and toolbar items from
//                     it, validates them, applies a key set and the user's own
//                     bindings, and reads two-key chords ("Ctrl+K,C")
//
//  Key text is GRIDEd's spelling with the Mac's keys added:
//    "Cmd+Shift+B", "Ctrl+F5", "Alt+Cmd+0" (Alt = Option), "Ctrl+K,C" (a chord)
//  Ctrl is the Control key, Cmd the Command key - a binding written on Windows
//  ("Ctrl+Shift+B") means Control on the Mac.
//
//  Key sets (Settings > Keyboard, GrideSettings::KeySet):
//    "macos"         the Mac's own (the default for GRIDEm)
//    "visualstudio"  GRIDEd's defaults for building, debugging and the F-keys
//    "gride"         RIDE's keys over Visual Studio's (GRIDEd's KeySets::For)
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

typedef void (^GRCommandHandler)(void);
typedef BOOL (^GRCommandTest)(void);

// -----------------------------------------------------------------------------
@interface GRCommand : NSObject

@property (nonatomic, copy, readonly) NSString* commandId;
@property (nonatomic, copy) NSString* title;              // no '&' accelerators on the Mac
@property (nonatomic, copy) NSString* category;           // the top menu it is listed under
@property (nonatomic, copy) NSString* symbolName;         // SF Symbol (may be nil)
@property (nonatomic, copy) NSString* shortcutText;       // in force: "Cmd+B", "Ctrl+K,C", "" for none
// a selector sent down the responder chain instead of the handler (undo:, copy:, saveDocument: ...)
@property (nonatomic, assign) SEL responderAction;
@property (nonatomic, assign) NSInteger responderTag;     // the tag the responder reads (NSTextFinder actions)
@property (nonatomic, copy) GRCommandHandler handler;
@property (nonatomic, copy) GRCommandTest canExecute;      // nil: enabled whenever it has a handler
@property (nonatomic, copy) GRCommandTest isChecked;       // nil: never checked

- (instancetype)initWithId:(NSString*)commandId title:(NSString*)title category:(NSString*)category
                    symbol:(NSString*)symbol;
- (BOOL)enabled;
- (BOOL)checked;
- (void)execute;
- (BOOL)isChord;                  // "Ctrl+K,C"
@end

// -----------------------------------------------------------------------------
@interface GRCommandRegistry : NSObject <NSMenuItemValidation, NSToolbarItemValidation>

+ (GRCommandRegistry*)shared;

@property (nonatomic, readonly) NSArray<GRCommand*>* all;  // in registration order
@property (nonatomic, readonly) NSString* keySet;           // "macos", "visualstudio", "gride"

- (GRCommand*)command:(NSString*)commandId;                 // nil when unknown
// adds a command that is not in the table (View.Toolbar*, external tools ...)
- (GRCommand*)addCommand:(NSString*)commandId title:(NSString*)title category:(NSString*)category
                  symbol:(NSString*)symbol shortcut:(NSString*)shortcut;
// handler and tests for a command of the table; returns the command
- (GRCommand*)bind:(NSString*)commandId handler:(GRCommandHandler)handler;
- (GRCommand*)bind:(NSString*)commandId handler:(GRCommandHandler)handler when:(GRCommandTest)canExecute;
- (void)setChecked:(NSString*)commandId test:(GRCommandTest)isChecked;
- (void)execute:(NSString*)commandId;

// the key set, then the user's own bindings over it (id -> key text; "" removes a key)
- (void)applyKeySet:(NSString*)keySet bindings:(NSDictionary<NSString*, NSString*>*)bindings;
// the shortcut a command has in a key set before any user binding
- (NSString*)defaultShortcut:(NSString*)commandId inKeySet:(NSString*)keySet;
// what Settings > Keyboard says of a key set
+ (NSString*)describeKeySet:(NSString*)keySet;
+ (NSArray<NSString*>*)keySetNames;

// ---- menus and toolbars ------------------------------------------------------
// the whole menu bar: GRIDEm, File, Edit, Find, View, Project, Build, Debug, Tools,
// Window, Help (GRIDEd's BuildMenus with the Mac's additions). The recent-file and
// recent-workspace submenus are left empty, tagged kGRRecentFilesTag / kGRRecentWorkspacesTag.
- (NSMenu*)buildMainMenu;
- (NSMenuItem*)menuItem:(NSString*)commandId;
- (NSToolbarItem*)toolbarItem:(NSString*)commandId showText:(BOOL)showText;
// re-applies titles, key equivalents and images after a key-set or theme change
- (void)refreshMenu:(NSMenu*)menu;

// ---- chords ----------------------------------------------------------------
// starts watching key-downs for two-key chords; status (may be nil) is told
// "Ctrl+K was pressed. Waiting for second key..." as GRIDEd's status bar says it
- (void)startChordMonitor:(void (^)(NSString* status))status;
- (void)stopChordMonitor;

// ---- key text --------------------------------------------------------------
// "Cmd+Shift+B" -> key equivalent and modifiers; NO for a chord or bad text
+ (BOOL)parseKey:(NSString*)text equivalent:(NSString**)equivalent modifiers:(NSEventModifierFlags*)modifiers;
// "Cmd+Shift+B" -> "⇧⌘B" as menus show it
+ (NSString*)displayText:(NSString*)text;
// the key text of an event ("Ctrl+K"), for chords and the Keyboard page
+ (NSString*)keyTextOfEvent:(NSEvent*)event;
@end

extern const NSInteger kGRRecentFilesTag;
extern const NSInteger kGRRecentWorkspacesTag;
extern const NSInteger kGRToolbarsMenuTag;
extern const NSInteger kGRExternalToolsTag;
