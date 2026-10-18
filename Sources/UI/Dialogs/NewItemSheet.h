// =============================================================================
//  NewItemSheet.h - Project > Add New Item (GRIDEd's NewItemDialog)
//  An item template, the file name and the project folder it goes into.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Model/GrideProject.h"

@interface GRNewItemSheet : GRSheet
- (instancetype)initWithProject:(const GRIDEm::Model::GrideProjectPtr&)project;
// ---- results (read after OK) ----
@property (nonatomic, readonly) NSString* filePath;     // absolute
@property (nonatomic, readonly) NSString* folder;       // the project folder (logical group)
@end
