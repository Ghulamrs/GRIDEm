// =============================================================================
//  ConvertSheet.h - Tools > Convert > C <-> Shalimar (GRIDEd's ConvertDialog)
//  The source, the direction, the output and the converter's log. The
//  conversion runs off the main thread; the sheet closes when it wrote the file.
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Services/CodeConverter.h"
#include "../../Settings/GrideSettings.h"

@interface GRConvertSheet : GRSheet
- (instancetype)initWithSettings:(const GRIDEm::Settings::ConverterSettingsPtr&)settings
                           input:(NSString*)input
                       direction:(GRIDEm::Services::ConvertDirection)direction;
@property (nonatomic, readonly) NSString* outputPath;     // after OK: the file written
@end
