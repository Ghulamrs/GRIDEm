// =============================================================================
//  AboutPanel.mm - see AboutPanel.h
// =============================================================================
#import "AboutPanel.h"
#import "DialogKit.h"
#include "../../Platform/Paths.h"
#include "../../Services/ToolVersions.h"

using namespace GRIDEm;

void GRShowAboutPanel(NSString* settingsPath)
{
    std::string settings = GRStd(settingsPath);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string text = Services::ToolVersions::Text(Platform::Paths::AppDirectory(), settings);
        dispatch_async(dispatch_get_main_queue(), ^{
            NSMutableParagraphStyle* para = [[NSMutableParagraphStyle alloc] init];
            para.alignment = NSTextAlignmentLeft;
            NSDictionary* attrs = @{
                NSFontAttributeName: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
                NSForegroundColorAttributeName: [NSColor labelColor],
                NSParagraphStyleAttributeName: para,
            };
            NSAttributedString* credits = [[NSAttributedString alloc] initWithString:GRNS(text) attributes:attrs];
            [NSApp orderFrontStandardAboutPanelWithOptions:@{
                NSAboutPanelOptionApplicationName: @"GRIDEm",
                NSAboutPanelOptionApplicationVersion: GROr([NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
                                                           GRNS(Services::ToolVersions::IdeVersion)),
                NSAboutPanelOptionCredits: credits,
            }];
        });
    });
}
