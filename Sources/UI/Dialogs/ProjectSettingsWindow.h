// =============================================================================
//  ProjectSettingsWindow.h - Project > Properties (GRIDEd's ProjectPropertiesDialog)
//  A sidebar of pages:
//    General           the project's targets and the projects it depends on
//    Configurations    per Config|Target: output and intermediate folders, output
//                      name, defines, include folders, optimisation, libraries,
//                      library folders, the linker .cmd, extra flags
//    Compiler          the project's compiler and a compiler per folder
//    Compiler Options  RIDE's Compiler Options (GRCompilerOptionsView)
//    Debugging         the program's arguments, Emulate only (TMS320C6747)
//  A project read from CCS or Visual Studio files is read-only: only what GRIDE
//  keeps for it in the workspace (compiler words, arguments, Emulate only) can
//  be changed. OK writes the .pro (or marks the workspace changed).
// =============================================================================
#pragma once
#import "Sheet.h"
#include "../../Model/GrideProject.h"
#include "../../Model/Workspace.h"

extern NSString* const GRProjectPageGeneral;
extern NSString* const GRProjectPageConfigurations;
extern NSString* const GRProjectPageCompiler;
extern NSString* const GRProjectPageCompilerOptions;   // Project > Compiler Options opens here
extern NSString* const GRProjectPageDebugging;

@interface GRProjectSettingsWindow : GRSheet
- (instancetype)initWithProject:(const GRIDEm::Model::GrideProjectPtr&)project
                      workspace:(const GRIDEm::Model::WorkspacePtr&)workspace
                         config:(NSString*)config
                         target:(GRIDEm::Core::Target)target
                       startPage:(NSString*)startPage;
// the title's note for a read-only project, in the words of its kind
+ (NSString*)readOnlyNote:(const GRIDEm::Model::GrideProjectPtr&)project;
// set when OK could not write the .pro (the sheet still closes; the caller reports it)
@property (nonatomic, readonly) NSString* saveError;
@end
