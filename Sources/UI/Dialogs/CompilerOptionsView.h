// =============================================================================
//  CompilerOptionsView.h - RIDE's Compiler Options as a panel (GRIDEd's
//  UI/Dialogs/CompilerOptionsEditor.h): one tab per toolchain.json
//  compilerOptions tab, a control per option (check box, choice, text), the
//  configuration chosen above, an option that means nothing for the target
//  greyed with the reason as its tip, the command line the tab's compiler
//  would get shown below, "Restore defaults" per configuration. Edits a draft
//  of a store; -commit copies the draft back. Used by Settings > Compiler
//  Options (the settings' store) and Project Settings (the project's).
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>
#include "../../Core/LanguageTarget.h"
#include "../../Settings/CompilerOptions.h"

@interface GRCompilerOptionsView : NSView
- (instancetype)initWithStore:(const GRIDEm::Settings::CompilerOptionStorePtr&)store
                       config:(NSString*)config
                       target:(GRIDEm::Core::Target)target
                     readOnly:(BOOL)readOnly;
@property (nonatomic, readonly) NSString* config;      // "Debug" / "Release"
// the target availability is judged for (Project Settings changes it)
- (void)setTarget:(GRIDEm::Core::Target)target;
// the draft into the store it came from
- (void)commit;
- (GRIDEm::Settings::CompilerOptionStorePtr)draft;
@end
