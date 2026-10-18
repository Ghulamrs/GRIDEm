// =============================================================================
//  Inspector.h - the Properties pane (GRIDEd's PropertiesPane and the "view"
//  classes of UI/PropertyObjects.h: FileProperties, ProjectProperties,
//  ConfigurationView, WorkspaceProperties)
//
//  GRPropertyItem   one row: category, name, description, value, read-only,
//                   a check box for a yes/no value, and what setting it does
//  GRPropertySheet  the rows for a file, a project, a configuration or the
//                   workspace - also what Project Settings shows for a configuration
//  GRInspector      the pane: the rows by category, as a form (NSGridView)
// =============================================================================
#pragma once
#import "PaneHost.h"
#include "../../Model/Workspace.h"
#include "../../Settings/GrideSettings.h"

@interface GRPropertyItem : NSObject
@property (nonatomic, copy) NSString* category;
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* help;            // GRIDEd's Description attribute
@property (nonatomic, copy) NSString* value;           // "Yes"/"No" for a check box
@property (nonatomic, assign) BOOL readOnly;
@property (nonatomic, assign) BOOL isBool;
@property (nonatomic, copy) void (^setter)(NSString* value);    // nil when read-only
+ (GRPropertyItem*)category:(NSString*)category name:(NSString*)name value:(NSString*)value;
@end

@interface GRPropertySheet : NSObject
+ (NSArray<GRPropertyItem*>*)forFile:(NSString*)path project:(GRIDEm::Model::GrideProjectPtr)project
                            settings:(GRIDEm::Settings::GrideSettingsPtr)settings;
+ (NSArray<GRPropertyItem*>*)forProject:(GRIDEm::Model::GrideProjectPtr)project;
// editable: OutDir, ObjDir, OutputName, Defines, IncludeDirs, Optimise, flags, LinkerCmd, Libs, LibDirs
+ (NSArray<GRPropertyItem*>*)forConfiguration:(GRIDEm::Model::BuildConfigurationPtr)configuration;
+ (NSArray<GRPropertyItem*>*)forWorkspace:(GRIDEm::Model::WorkspacePtr)workspace;
@end

@interface GRInspector : GRPaneController
- (instancetype)init;                                     // "Properties", "Properties"
// shows the rows (nil or empty: "Nothing selected"); title names what they are of
- (void)setItems:(NSArray<GRPropertyItem*>*)items title:(NSString*)title;
// reads the values again (after a build or a save changed them)
- (void)refreshValues:(NSArray<GRPropertyItem*>*)items;
// told after a row's value was set, so the window can save or mark dirty
@property (nonatomic, copy) void (^valueChanged)(GRPropertyItem* item);
@end

// a form of property rows in a view, for the inspector and for dialogs
@interface GRPropertyForm : NSView
@property (nonatomic, copy) void (^valueChanged)(GRPropertyItem* item);
- (void)setItems:(NSArray<GRPropertyItem*>*)items;
@end
