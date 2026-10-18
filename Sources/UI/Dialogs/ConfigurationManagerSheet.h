// =============================================================================
//  ConfigurationManagerSheet.h - Build > Configuration Manager
//  (GRIDEd's ConfigurationManagerDialog): the active configuration and target,
//  and which projects are built for each target. OK sets the workspace's
//  active pair and saves the projects whose targets changed.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Model/Workspace.h"

@interface GRConfigurationManagerSheet : GRSheet
- (instancetype)initWithWorkspace:(const GRIDEm::Model::WorkspacePtr&)workspace;
@end
