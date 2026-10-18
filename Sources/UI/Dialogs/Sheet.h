// =============================================================================
//  Sheet.h - the base of GRIDEm's dialogs (GRIDEd's UI/Dialogs/DialogBase.h:
//  ThemedDialog, InputDialog, TextDialog)
//
//  GRSheet       a panel shown as a sheet on a window (modal when there is none),
//                with its content above and the buttons below: OK / Cancel on
//                the right (Return / Escape), extra buttons on the left. A
//                subclass fills -content and overrides -accept, which says
//                whether OK may close it (GRIDEd's OnAccept).
//  GRInputSheet  one line of text asked for
//  GRTextSheet   a text shown read-only (a report, a mapping), with Copy
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@interface GRSheet : NSWindowController

@property (nonatomic, readonly) NSView* content;          // fill this (or call -setContentView:)
@property (nonatomic, readonly) NSButton* okButton;       // nil after -removeOkButton
@property (nonatomic, readonly) NSButton* cancelButton;
@property (nonatomic, readonly) NSWindow* parentWindow;
@property (nonatomic, readonly) BOOL accepted;

- (instancetype)initWithTitle:(NSString*)title size:(NSSize)size;
// the view that fills the content area
- (void)setContentView:(NSView*)view;
- (void)setOkTitle:(NSString*)title;
- (void)setCancelTitle:(NSString*)title;
// a dialog with one closing button only ("Close")
- (void)removeOkButton;
// a button on the left of the bar
- (NSButton*)addButton:(NSString*)title action:(void (^)(void))action;
// a button on the right, before OK
- (NSButton*)addTrailingButton:(NSString*)title action:(void (^)(void))action;

// shows it; done is told whether it closed with OK
- (void)beginOn:(NSWindow*)parent completion:(void (^)(BOOL ok))done;
// closes it (as OK or as Cancel) without asking -accept
- (void)finish:(BOOL)ok;

// ---- for subclasses ----
- (BOOL)accept;                 // YES lets OK close the sheet
- (void)showMessage:(NSString*)text;   // a message over the sheet
@end

// -----------------------------------------------------------------------------
@interface GRInputSheet : GRSheet
@property (nonatomic, readonly) NSString* value;
- (instancetype)initWithTitle:(NSString*)title prompt:(NSString*)prompt value:(NSString*)value;
// empty text is refused with this message (nil: empty is allowed)
@property (nonatomic, copy) NSString* emptyMessage;
@end

// -----------------------------------------------------------------------------
@interface GRTextSheet : GRSheet
- (instancetype)initWithTitle:(NSString*)title text:(NSString*)text mono:(BOOL)mono;
@end
