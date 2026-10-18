// =============================================================================
//  AboutPanel.h - GRIDEm > About GRIDEm (GRIDEd's AboutDialog, as the Mac's
//  standard About panel): the IDE's version, then each tool's version as the
//  tool reports it (Services::ToolVersions), as the credits. The tools are
//  asked off the main thread; the panel opens when they have answered.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

// settingsPath: the settings file in use, named in the text
void GRShowAboutPanel(NSString* settingsPath);
