// =============================================================================
//  SnippetsSheet.h - Tools > Code Snippets (GRIDEd's SnippetsDialog)
//  Named snippets, their text edited beside the list; New, Delete, and Insert
//  (when there is an editor to insert into).
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Settings/GrideSettings.h"

@interface GRSnippetsSheet : GRSheet
- (instancetype)initWithSnippets:(const GRIDEm::Settings::OrderedStringMap&)snippets canInsert:(BOOL)canInsert;
// after OK: the snippets as edited
- (GRIDEm::Settings::OrderedStringMap)snippets;
// set when the sheet closed with Insert (nil otherwise)
@property (nonatomic, readonly) NSString* insertText;
@end
