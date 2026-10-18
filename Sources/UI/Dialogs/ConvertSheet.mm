// =============================================================================
//  ConvertSheet.mm - see ConvertSheet.h
// =============================================================================
#import "ConvertSheet.h"
#import "DialogKit.h"
#include "../../Core/PathUtil.h"
#include "../../Platform/Paths.h"

using namespace GRIDEm;

@implementation GRConvertSheet
{
    Settings::ConverterSettingsPtr _settings;
    GRPathField* _input;
    NSButton* _toShl;
    NSButton* _toC;
    NSTextField* _output;
    NSTextView* _log;
    NSProgressIndicator* _spinner;
    NSString* _outputPath;
    BOOL _running;
}

- (instancetype)initWithSettings:(const Settings::ConverterSettingsPtr&)settings input:(NSString*)input
                       direction:(Services::ConvertDirection)d
{
    if ((self = [super initWithTitle:@"Convert Source" size:NSMakeSize(660, 440)]))
    {
        _settings = settings;
        GRForm* form = [[GRForm alloc] init];
        form.labelWidth = 110;
        _input = [[GRPathField alloc] initFolder:NO status:NO];
        _input.value = input != nil ? input : @"";
        __weak GRConvertSheet* weakSelf = self;
        _input.changed = ^{ [weakSelf inputChanged]; };
        [form addRow:@"Source file" view:_input];
        _toShl = [NSButton radioButtonWithTitle:@"C  ->  Shalimar" target:self action:@selector(directionChanged:)];
        _toC = [NSButton radioButtonWithTitle:@"Shalimar  ->  C" target:self action:@selector(directionChanged:)];
        _toShl.state = d == Services::ConvertDirection::CToShalimar ? NSControlStateValueOn : NSControlStateValueOff;
        _toC.state = d == Services::ConvertDirection::ShalimarToC ? NSControlStateValueOn : NSControlStateValueOff;
        NSStackView* dir = [NSStackView stackViewWithViews:@[_toShl, _toC]];
        dir.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        dir.spacing = 18;
        [form addRow:@"Direction" view:dir];
        _output = [form addText:@"Output file" value:@""];
        _log = [form addLines:@"Log" text:@"" height:180 mono:YES];
        _log.editable = NO;
        _spinner = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(0, 0, 16, 16)];
        _spinner.style = NSProgressIndicatorStyleSpinning;
        _spinner.controlSize = NSControlSizeSmall;
        _spinner.displayedWhenStopped = NO;
        [form addRow:@"" view:_spinner];
        [self setContentView:form.view];
        [self setOkTitle:@"Convert"];
        [self suggest];
    }
    return self;
}

- (NSString*)outputPath { return _outputPath; }

- (Services::ConvertDirection)direction
{
    return _toShl.state == NSControlStateValueOn ? Services::ConvertDirection::CToShalimar
                                                 : Services::ConvertDirection::ShalimarToC;
}

- (void)suggest
{
    std::string src = GRStd(_input.value);
    if (src.empty()) return;
    Services::ConvertDirection d = [self direction];
    _output.stringValue = GRNS(Services::CodeConverter::SuggestOutput(src, d));
    Services::CodeConverter c(_settings);
    std::string tool = c.ToolPath(d);
    if (Platform::Paths::IsFile(tool)) _log.string = GRNS("Converter: " + tool);
    else
    {
        std::string looked = c.LookedIn(d);
        std::string text = "Converter not installed yet: " + tool;
        if (!looked.empty()) text += "\nLooked in: " + looked;
        text += "\nPut c2s inside GRIDEm.app (Contents/MacOS), or set the converter in Settings > Converters.";
        _log.string = GRNS(text);
    }
}

- (void)inputChanged
{
    std::string src = GRStd(_input.value);
    if (Platform::Paths::IsFile(src))
    {
        bool shl = Core::PathUtil::HasExtension(src, ".shl") || Core::PathUtil::HasExtension(src, ".shm");
        _toC.state = shl ? NSControlStateValueOn : NSControlStateValueOff;
        _toShl.state = shl ? NSControlStateValueOff : NSControlStateValueOn;
    }
    [self suggest];
}

- (void)directionChanged:(id)sender
{
    (void)sender;
    // the two radio buttons share a superview, so AppKit keeps one of them on
    [self suggest];
}

- (BOOL)accept
{
    if (_running) return NO;
    std::string src = GRStd(_input.value);
    if (!Platform::Paths::IsFile(src)) { _log.string = @"Source file not found."; return NO; }
    std::string out = GRStd([_output.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]]);
    Services::ConvertDirection d = [self direction];
    Settings::ConverterSettingsPtr settings = _settings;
    _running = YES;
    self.okButton.enabled = NO;
    [_spinner startAnimation:nil];
    __weak GRConvertSheet* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        Services::CodeConverter c(settings);
        bool ok = c.Convert(src, out, d);
        std::string log = c.Log();
        dispatch_async(dispatch_get_main_queue(), ^{
            GRConvertSheet* me = weakSelf;
            if (me == nil) return;
            me->_running = NO;
            [me->_spinner stopAnimation:nil];
            me.okButton.enabled = YES;
            me->_log.string = GRNS(log);
            if (!ok) return;
            me->_outputPath = GRNS(out);
            [me finish:YES];
        });
    });
    return NO;
}
@end
