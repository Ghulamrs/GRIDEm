// =============================================================================
//  NewProjectSheet.h - File > New > Project (GRIDEd's NewProjectDialog)
//  Language, a template from a grid, the folder layout it creates, the name,
//  the location, the workspace it goes into and the targets. "Import CCS 7.4 /
//  5.5..." as a language asks the caller to open a CCS project instead.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Core/LanguageTarget.h"
#include <string>
#include <vector>

@interface GRNewProjectSheet : GRSheet
- (instancetype)initWithLocation:(NSString*)defaultLocation haveWorkspace:(BOOL)haveWorkspace;

// ---- results (read after OK) ----
@property (nonatomic, readonly) NSString* projectName;
@property (nonatomic, readonly) NSString* projectLocation;
@property (nonatomic, readonly) GRIDEm::Core::Language language;
@property (nonatomic, readonly) NSString* templateName;
@property (nonatomic, readonly) BOOL newWorkspace;
@property (nonatomic, readonly) BOOL importCcs;     // the caller opens File > Open > CCS Project
- (std::vector<GRIDEm::Core::Target>)targets;
@end
