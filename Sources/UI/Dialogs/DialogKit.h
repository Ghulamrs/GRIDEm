// =============================================================================
//  DialogKit.h - the pieces every GRIDEm dialog is built from
//  (GRIDEd's UI/Dialogs/DialogBase.h helpers - MakeGrid, AddRow, Heading -
//  and OptionsDialog.h's PathField, said the AppKit way)
//
//    GRNS / GRStd / GRLines ...  std::string <-> NSString, lists <-> lines
//    GRForm         a scrolling two-column form: sections, label + control rows
//    GRPathField    a path box with "Choose..." and a Found / Not installed status
//    GRCheckList    a list of check boxes (targets, dependencies, batch items)
//    GRTable        a view-based table of text / editable text / pop-up columns
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include <string>
#include <vector>
#import "../Common.h"

// ---- strings: GRNS / GRStd are in ../Common.h -----------------------------------
// one entry per line; GRIDEd's Lines() / FromLines()
NSString* GRJoinLines(const std::vector<std::string>& items);
// the lines of a text, trimmed, empty ones dropped
std::vector<std::string> GRSplitLines(NSString* text);
// "a; b; c" -> entries; GRIDEd's Split on ';'
std::vector<std::string> GRSplitSemicolons(NSString* text);
NSString* GRJoinSemicolons(const std::vector<std::string>& items);
NSArray<NSString*>* GRArray(const std::vector<std::string>& items);
std::vector<std::string> GRVector(NSArray<NSString*>* items);

// a message on a window as a sheet (a plain alert when window is nil)
void GRShowMessage(NSWindow* window, NSString* title, NSString* text);
// Yes / No on a window; the answer comes to done on the main thread
void GRAskYesNo(NSWindow* window, NSString* title, NSString* text, void (^done)(BOOL yes));

// ---- the form ------------------------------------------------------------------
@interface GRForm : NSObject
@property (nonatomic, readonly) NSScrollView* view;       // put this into a window or a tab
@property (nonatomic, readonly) NSGridView* grid;
@property (nonatomic) CGFloat labelWidth;                 // 170 by default
- (instancetype)init;
- (void)addSection:(NSString*)title;
// a row: the label (may be empty) and the control
- (void)addRow:(NSString*)label view:(NSView*)view;
// a muted note across the control column
- (NSTextField*)addNote:(NSString*)text;
// ready-made controls, added as a row and returned
- (NSTextField*)addText:(NSString*)label value:(NSString*)value;
- (NSTextField*)addMono:(NSString*)label value:(NSString*)value;
- (NSButton*)addCheck:(NSString*)title on:(BOOL)on;
- (NSPopUpButton*)addPopup:(NSString*)label items:(NSArray<NSString*>*)items selected:(NSString*)selected;
- (NSTextField*)addNumber:(NSString*)label value:(double)value min:(double)min max:(double)max decimals:(int)decimals;
// a multi-line text box (one entry per line); height in points
- (NSTextView*)addLines:(NSString*)label text:(NSString*)text height:(CGFloat)height mono:(BOOL)mono;
@end

// ---- a path with its status ------------------------------------------------------
typedef BOOL (^GRPathCheck)(NSString* value);

@interface GRPathField : NSView
@property (nonatomic, readonly) NSTextField* field;
@property (nonatomic, copy) NSString* value;
@property (nonatomic, copy) NSString* baseDir;            // relative names are resolved against it
@property (nonatomic) BOOL folder;                        // chooses folders, not files
@property (nonatomic) BOOL showStatus;
// how Found is decided; nil: the file (or folder) exists, after $VAR expansion and baseDir
@property (nonatomic, copy) GRPathCheck check;
@property (nonatomic, copy) void (^changed)(void);
- (instancetype)initFolder:(BOOL)folder status:(BOOL)status;
- (void)updateStatus;
@end

// ---- check boxes in a list ---------------------------------------------------------
@interface GRCheckList : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, readonly) NSScrollView* view;
@property (nonatomic, readonly) NSTableView* table;
@property (nonatomic, strong) NSMutableArray<NSString*>* titles;
@property (nonatomic, strong) NSMutableArray<NSNumber*>* checks;
@property (nonatomic) BOOL enabled;
@property (nonatomic, copy) void (^toggled)(NSInteger row, BOOL on);
@property (nonatomic, copy) void (^selected)(NSInteger row);
- (instancetype)initWithHeight:(CGFloat)height;
- (void)setItems:(NSArray<NSString*>*)titles checked:(NSArray<NSNumber*>*)checks;
- (BOOL)isChecked:(NSInteger)row;
- (void)setAll:(BOOL)on;
- (NSArray<NSString*>*)checkedTitles;
@end

// ---- a table ---------------------------------------------------------------------
typedef NS_ENUM(NSInteger, GRColumnKind) { GRColumnText, GRColumnEdit, GRColumnPopup };

@interface GRTable : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property (nonatomic, readonly) NSScrollView* view;
@property (nonatomic, readonly) NSTableView* table;
// rows of cells; a cell is the text (a pop-up column's selected choice)
@property (nonatomic, strong) NSMutableArray<NSMutableArray<NSString*>*>* rows;
@property (nonatomic, strong) NSMutableArray* tags;      // one object per row for the caller (may be NSNull)
@property (nonatomic, copy) void (^edited)(NSInteger row, NSInteger column, NSString* value);
@property (nonatomic, copy) void (^selectionChanged)(void);
@property (nonatomic, copy) void (^doubleClicked)(NSInteger row);
@property (nonatomic) BOOL readOnly;
- (instancetype)initWithHeight:(CGFloat)height;
- (void)addColumn:(NSString*)title width:(CGFloat)width kind:(GRColumnKind)kind choices:(NSArray<NSString*>*)choices;
- (void)addRow:(NSArray<NSString*>*)cells tag:(id)tag;
- (void)clear;
- (void)reload;
- (NSInteger)selectedRow;
- (NSIndexSet*)selectedRows;
// colours a column's text in a row (status columns)
- (void)setColor:(NSColor*)color row:(NSInteger)row column:(NSInteger)column;
@end
