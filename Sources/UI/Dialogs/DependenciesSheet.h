// =============================================================================
//  DependenciesSheet.h - Project > Project Dependencies (GRIDEd's DependenciesDialog)
//  For each project, the projects it depends on (built first).
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Model/Workspace.h"

@interface GRDependenciesSheet : GRSheet
- (instancetype)initWithWorkspace:(const GRIDEm::Model::WorkspacePtr&)workspace
                         selected:(const GRIDEm::Model::GrideProjectPtr&)selected;
@end
