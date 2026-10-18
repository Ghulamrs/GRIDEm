// =============================================================================
//  ExternalToolsSheet.h - Tools > External Tools (GRIDEd's ExternalToolsDialog)
//  Each entry's title, command, arguments and initial folder; Add, Remove.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Settings/GrideSettings.h"
#include <vector>

@interface GRExternalToolsSheet : GRSheet
- (instancetype)initWithTools:(const std::vector<GRIDEm::Settings::ExternalToolPtr>&)tools;
// after OK: the edited list (copies; the caller puts them into the settings)
- (std::vector<GRIDEm::Settings::ExternalToolPtr>)tools;
@end
