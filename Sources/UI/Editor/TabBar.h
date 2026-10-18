// =============================================================================
//  TabBar.h - GRTabBar: the editor's tab strip (GRIDEd's UI/Controls/TabStrip.h
//  and the tab context menu of DocumentManager.h)
//    click to select, the x (or a middle click) to close, a dot when modified,
//    pinned tabs first with a pin, drag to reorder, a chevron listing every tab
//    when they do not fit, and the context menu: Save, Close, Close Others,
//    Close All, Pin / Unpin, Copy Path, Show in Finder.
// =============================================================================
#pragma once
#import <AppKit/AppKit.h>

@class GRTabBar;

@interface GRTabItem : NSObject
@property (nonatomic, copy) NSString* title;
@property (nonatomic, copy) NSString* path;          // the tool tip and Copy Path; nil while untitled
@property (nonatomic, assign) BOOL modified;
@property (nonatomic, assign) BOOL pinned;
@property (nonatomic, assign) BOOL readOnly;
@property (nonatomic, strong) id representedObject;  // the GRSourceDocument
@end

@protocol GRTabBarDelegate <NSObject>
- (void)tabBar:(GRTabBar*)bar didSelect:(NSInteger)index;
- (void)tabBar:(GRTabBar*)bar close:(NSInteger)index;
@optional
- (void)tabBar:(GRTabBar*)bar save:(NSInteger)index;
- (void)tabBar:(GRTabBar*)bar closeOthers:(NSInteger)index;
- (void)tabBarCloseAll:(GRTabBar*)bar;
- (void)tabBar:(GRTabBar*)bar togglePin:(NSInteger)index;
- (void)tabBar:(GRTabBar*)bar moveFrom:(NSInteger)from to:(NSInteger)to;
@end

@interface GRTabBar : NSView

@property (nonatomic, weak) id<GRTabBarDelegate> delegate;
@property (nonatomic, readonly) NSArray<GRTabItem*>* items;
@property (nonatomic, assign) NSInteger selectedIndex;      // -1 for none

- (void)setItems:(NSArray<GRTabItem*>*)items selected:(NSInteger)selected;
- (void)reload;                                              // after an item's title / modified / pinned changed
- (NSInteger)indexOfObject:(id)represented;                  // NSNotFound when absent
@end
