// =============================================================================
//  WindowsSheet.h - Window > Windows… (GRIDEd's WindowsDialog)
//  Every open document; Activate, Save or Close the ones chosen.
// =============================================================================
#pragma once
#import "Sheet.h"

typedef NS_ENUM(NSInteger, GRWindowsAction) { GRWindowsActivate, GRWindowsSave, GRWindowsClose };

@interface GRWindowsSheet : GRSheet
- (instancetype)initWithTitles:(NSArray<NSString*>*)titles paths:(NSArray<NSString*>*)paths;
@property (nonatomic, readonly) GRWindowsAction action;
@property (nonatomic, readonly) NSIndexSet* selected;     // rows of titles
@end
