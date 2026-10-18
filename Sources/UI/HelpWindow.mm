// =============================================================================
//  HelpWindow.mm - see HelpWindow.h
// =============================================================================
#import "HelpWindow.h"
#import <WebKit/WebKit.h>

@interface GRHelpWindow () <WKNavigationDelegate>
@end

@implementation GRHelpWindow
{
    WKWebView* _web;
}

+ (GRHelpWindow*)shared
{
    static GRHelpWindow* one = nil;
    if (one == nil) one = [[GRHelpWindow alloc] init];
    return one;
}

- (instancetype)init
{
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 980, 760)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                        | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                backing:NSBackingStoreBuffered defer:YES];
    self = [super initWithWindow:w];
    if (self == nil) return nil;
    w.title = @"GRIDEm Help";
    w.releasedWhenClosed = NO;
    [w center];
    w.frameAutosaveName = @"GRIDEm.Help";
    WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
    _web = [[WKWebView alloc] initWithFrame:w.contentView.bounds configuration:config];
    _web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _web.navigationDelegate = self;
    w.contentView = _web;
    return self;
}

- (void)showFile:(NSString*)path
{
    NSURL* url = [NSURL fileURLWithPath:path];
    NSString* ext = path.pathExtension.lowercaseString;
    if ([ext isEqualToString:@"md"] || [ext isEqualToString:@"txt"])
    {
        NSString* text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:nil];
        if (text == nil) text = @"";
        NSString* escaped = [[[text stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"]
                              stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"]
                             stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
        NSString* html = [NSString stringWithFormat:@"<html><head><meta charset='utf-8'><style>body{font:13px -apple-system;"
                                                    "margin:24px;color-scheme:light dark}pre{white-space:pre-wrap;"
                                                    "font:12px ui-monospace,Menlo}</style></head><body><pre>%@</pre></body></html>",
                                                    escaped];
        [_web loadHTMLString:html baseURL:[url URLByDeletingLastPathComponent]];
    }
    else [_web loadFileURL:url allowingReadAccessToURL:[url URLByDeletingLastPathComponent]];
    self.window.title = path.lastPathComponent;
    [self showWindow:nil];
    [self.window makeKeyAndOrderFront:nil];
}

// a link out of the help (the web) goes to the browser
- (void)webView:(WKWebView*)webView decidePolicyForNavigationAction:(WKNavigationAction*)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    (void)webView;
    NSURL* u = action.request.URL;
    if (action.navigationType == WKNavigationTypeLinkActivated && u != nil && !u.isFileURL)
    {
        [[NSWorkspace sharedWorkspace] openURL:u];
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}
@end
