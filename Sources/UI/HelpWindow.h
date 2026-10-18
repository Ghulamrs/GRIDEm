// =============================================================================
//  HelpWindow.h - GRHelpWindow: the help and the references in a window of
//  their own (GRIDEd opens them with the system's viewer; the Mac shows the
//  shipped HTML, and PDFs, in a WKWebView). One window, reused.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@interface GRHelpWindow : NSWindowController
+ (GRHelpWindow*)shared;
// shows a file (.html, .htm, .pdf, .txt, .md) and brings the window forward
- (void)showFile:(NSString*)path;
@end
