// =============================================================================
//  ToolchainCheckSheet.h - Tools > Toolchain Check (GRIDEd's ToolchainCheckDialog)
//  toolchain.json's files and problems, then every tool of every target with
//  where it resolved, Found / Not installed and where it was looked for, then
//  the converters; a count of found and missing at the top.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Settings/GrideSettings.h"

@interface GRToolchainCheckSheet : GRSheet
- (instancetype)initWithSettings:(const GRIDEm::Settings::GrideSettingsPtr&)settings;
// YES when it closed with "Open Settings…" (the caller opens Settings > Toolchains)
@property (nonatomic, readonly) BOOL openSettings;
@end
