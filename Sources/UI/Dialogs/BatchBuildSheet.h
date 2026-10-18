// =============================================================================
//  BatchBuildSheet.h - Build > Batch Build (GRIDEd's BatchBuildDialog)
//  Every Project | Config | Target the workspace can build, checked to build,
//  rebuild or clean in one run.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Model/Workspace.h"
#include <string>
#include <vector>

// GRIDEd's BuildAction as the batch chooses it
typedef NS_ENUM(NSInteger, GRBatchAction) { GRBatchBuild, GRBatchRebuild, GRBatchClean };

struct GRBatchItem
{
    GRIDEm::Model::GrideProjectPtr Project;
    std::string Config;
    GRIDEm::Core::Target TargetKind;
};

@interface GRBatchBuildSheet : GRSheet
- (instancetype)initWithWorkspace:(const GRIDEm::Model::WorkspacePtr&)workspace;
@property (nonatomic, readonly) GRBatchAction action;
- (std::vector<GRBatchItem>)selected;
@end
