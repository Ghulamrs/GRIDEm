// =============================================================================
//  DefinitionsPage.mm - Settings > Definitions (toolchain.json)
//  (GRIDEd's OptionsDialog ToolchainDefinitionsPage)
//  Edits the user's overrides of the shipped toolchain file: a tool's program
//  name, aliases, words, lookup, flags and operations, a library's files and
//  directories. Only what differs from the shipped file is written to the
//  user's file; Restore puts the shipped entry back. After saving, the
//  definitions are reloaded and every toolchain value still at its old
//  default follows the new one; what the user set stays.
// =============================================================================
#import "SettingsWindow.h"
#import "DialogKit.h"
#include "../../Core/Json.h"
#include "../../Core/StringUtil.h"
#include "../../Settings/ToolchainDefinitions.h"
#include "../../Settings/ToolchainSettings.h"
#include <map>
#include <set>

using namespace GRIDEm;
using Core::JsonPtr;
using Core::JsonValue;
using Core::JsonWriter;
using Settings::ToolchainDefinitions;

// ---- the JSON helpers of GRIDEd's page ----------------------------------------------
static NSString* Joined(const JsonPtr& o, const char* key)
{
    if (!o) return @"";
    return GRNS(Core::StringUtil::Join("; ", o->GetStringList(key)));
}

static NSString* Nested(const JsonPtr& o, const char* key, const char* sub)
{
    JsonPtr n = o ? o->GetObject(key) : JsonPtr();
    return n ? GRNS(n->GetString(sub, "")) : @"";
}

static bool NestedBool(const JsonPtr& o, const char* key, const char* sub, bool def)
{
    JsonPtr n = o ? o->GetObject(key) : JsonPtr();
    return n ? n->GetBool(sub, def) : def;
}

static JsonPtr Sub(const JsonPtr& o, const std::string& key)
{
    JsonPtr n = o->GetObject(key);
    if (!n)
    {
        n = JsonValue::MakeObject();
        o->SetValue(key, n);
    }
    return n;
}

static bool Same(const JsonPtr& a, const JsonPtr& b)
{
    std::string x = a ? JsonWriter::Write(a) : std::string("null");
    std::string y = b ? JsonWriter::Write(b) : std::string("null");
    return x == y;
}

static JsonPtr At(const JsonPtr& o, const std::string& key, const char* sub)
{
    if (!o) return JsonPtr();
    if (sub == nullptr) return o->Get(key);
    JsonPtr n = o->GetObject(key);
    return n ? n->Get(sub) : JsonPtr();
}

// a scalar or list at [key] (or [key][sub]): set in the override when it differs from the shipped value,
// removed when not
static void DiffAt(const JsonPtr& ov, const JsonPtr& view, const JsonPtr& shipped, const std::string& key, const char* sub)
{
    JsonPtr v = At(view, key, sub);
    JsonPtr s = At(shipped, key, sub);
    bool differs = !Same(v, s);
    if (sub == nullptr)
    {
        if (differs) ov->SetValue(key, ToolchainDefinitions::Clone(v));
        else ov->Remove(key);
        return;
    }
    JsonPtr n = ov->GetObject(key);
    if (differs)
    {
        if (!n) { n = JsonValue::MakeObject(); ov->SetValue(key, n); }
        n->SetValue(sub, ToolchainDefinitions::Clone(v));
    }
    else if (n)
    {
        n->Remove(sub);
        if (n->Count() == 0) ov->Remove(key);
    }
}

static void ApplyToolDiff(const JsonPtr& ov, const JsonPtr& view, const JsonPtr& shipped)
{
    for (const char* k : { "exe", "aliases", "words", "outputEncoding", "diagnostics", "archFlag", "includeFlag",
                           "defineFlag", "installIncludeDirs" })
        DiffAt(ov, view, shipped, k, nullptr);
    for (const char* k : { "env", "installDir", "path", "vcvars" }) DiffAt(ov, view, shipped, "lookup", k);
    for (const char* k : { "Debug", "Release" }) DiffAt(ov, view, shipped, "configFlags", k);
    for (const char* k : { "codeview", "assemblerNamed" }) DiffAt(ov, view, shipped, "asmSyntaxFlags", k);
    DiffAt(ov, view, shipped, "version", "args");
    JsonPtr vops = view->GetObject("operations");
    JsonPtr sops = shipped ? shipped->GetObject("operations") : JsonPtr();
    if (!vops) return;
    for (const std::string& op : vops->KeyNames())
    {
        JsonPtr vo = vops->GetObject(op);
        JsonPtr so = sops ? sops->GetObject(op) : JsonPtr();
        if (!vo) continue;
        JsonPtr oops = ov->GetObject("operations");
        if (!Same(vo->Get("args"), so ? so->Get("args") : JsonPtr()))
        {
            if (!oops) { oops = JsonValue::MakeObject(); ov->SetValue("operations", oops); }
            JsonPtr oo = oops->GetObject(op);
            if (!oo) { oo = JsonValue::MakeObject(); oops->SetValue(op, oo); }
            oo->SetString("args", vo->GetString("args", ""));
        }
        else if (oops && oops->GetObject(op))
        {
            oops->GetObject(op)->Remove("args");
            if (oops->GetObject(op)->Count() == 0) oops->Remove(op);
            if (oops->Count() == 0) ov->Remove("operations");
        }
    }
}

static void ApplyLibDiff(const JsonPtr& ov, const JsonPtr& view, const JsonPtr& shipped)
{
    for (const char* k : { "directory", "directoryEnv", "marker", "aliases", "when" }) DiffAt(ov, view, shipped, k, nullptr);
    for (const char* k : { "Debug", "Release" })
    {
        DiffAt(ov, view, shipped, "files", k);
        DiffAt(ov, view, shipped, "assemblyDirectories", k);
    }
}

static std::vector<std::string> SplitEntries(NSString* s) { return GRSplitSemicolons(s); }

static std::string Trimmed(NSTextField* f) { return Core::StringUtil::Trim(GRStd(f.stringValue)); }

// -----------------------------------------------------------------------------
@implementation GRDefinitionsPage
{
    Settings::ToolchainDefinitionsPtr _defs;
    std::map<std::string, JsonPtr> _toolView;     // what the page shows per tool (the merged entry, edited)
    std::map<std::string, JsonPtr> _libView;
    std::set<std::string> _touched;                // "tools:id" / "libraries:id"
    std::string _currentTool;
    std::string _currentLib;
    NSPopUpButton* _toolBox;
    NSTextField* _toolDesc;
    NSTextField* _toolState;
    NSTextField *_exe, *_aliases, *_words, *_envVar, *_installDir, *_versionArgs, *_encoding, *_diagnostics;
    NSTextField *_archFlag, *_includeFlag, *_defineFlag, *_installIncludes, *_debugFlags, *_releaseFlags;
    NSTextField *_codeviewFlag, *_namedAsmFlag;
    NSButton *_onPath, *_vcvars;
    GRTable* _ops;
    NSPopUpButton* _libBox;
    NSTextField* _libDesc;
    NSTextField* _libState;
    NSTextField *_libDir, *_libEnv, *_libMarker, *_libDebug, *_libRelease, *_libAliases, *_libAsmDebug, *_libAsmRelease, *_libWhen;
}

- (NSView*)makeContent
{
    _defs = ToolchainDefinitions::Current();
    GRForm* form = [[GRForm alloc] init];
    [form addSection:@"toolchain.json"];
    std::string files = "Shipped: " + (_defs->ShippedFound ? _defs->ShippedFile : "not found (" + _defs->ShippedFile + ")")
        + "\nYour overrides: " + _defs->UserFile + (_defs->UserJson ? "" : " (none yet)")
        + (_defs->Problems.empty() ? std::string("\nThe file reads clean.")
                                   : "\n" + Core::StringUtil::Join("\n", _defs->Problems));
    [form addNote:GRNS(files)];

    [form addSection:@"Tools"];
    NSMutableArray* tools = [NSMutableArray array];
    for (const Settings::ToolDefinitionPtr& t : _defs->Tools)
        [tools addObject:GRNS(t->Id + "  -  " + t->Name + "  (" + t->Role + ")")];
    _toolBox = [form addPopup:@"Tool" items:tools selected:nil];
    _toolBox.target = self;
    _toolBox.action = @selector(toolChosen:);
    _toolDesc = [form addNote:@""];
    _toolState = [form addText:@"Source" value:@""];
    _toolState.editable = NO;
    _toolState.bezeled = NO;
    _toolState.drawsBackground = NO;
    _exe = [form addMono:@"Program (exe)" value:@""];
    _aliases = [form addMono:@"Aliases (;)" value:@""];
    _words = [form addMono:@"Project words (;)" value:@""];
    _envVar = [form addMono:@"Environment variable" value:@""];
    _installDir = [form addMono:@"Installation sub-folder" value:@""];
    _onPath = [form addCheck:@"Also looked for on PATH" on:YES];
    _vcvars = [form addCheck:@"Reached through vcvars64.bat (Visual Studio's - Windows only)" on:NO];
    _versionArgs = [form addMono:@"Version query" value:@""];
    _encoding = [form addMono:@"Output encoding" value:@""];
    _diagnostics = [form addMono:@"Diagnostics shape" value:@""];
    _archFlag = [form addMono:@"Target flag" value:@""];
    _includeFlag = [form addMono:@"Include flag" value:@""];
    _defineFlag = [form addMono:@"Define flag" value:@""];
    _installIncludes = [form addMono:@"Installation includes (;)" value:@""];
    _debugFlags = [form addMono:@"Debug flags" value:@""];
    _releaseFlags = [form addMono:@"Release flags" value:@""];
    _codeviewFlag = [form addMono:@"CodeView syntax flag" value:@""];
    _namedAsmFlag = [form addMono:@"Named-assembler syntax flag" value:@""];
    _ops = [[GRTable alloc] initWithHeight:150];
    [_ops addColumn:@"Operation" width:140 kind:GRColumnText choices:nil];
    [_ops addColumn:@"Arguments" width:320 kind:GRColumnEdit choices:nil];
    [_ops addColumn:@"What it does" width:200 kind:GRColumnText choices:nil];
    [form addRow:@"Operations" view:_ops.view];
    std::vector<std::string> placeholders;
    for (const std::pair<const std::string, std::string>& kv : _defs->Placeholders) placeholders.push_back(kv.first);
    [form addNote:GRNS("Placeholders: " + Core::StringUtil::Join(" ", placeholders)
                       + " - each is explained in toolchain.json and TOOLCHAIN-FORMAT.md.")];
    [form addRow:@"" view:[NSButton buttonWithTitle:@"Restore shipped defaults (this tool)" target:self action:@selector(restoreTool:)]];

    [form addSection:@"Libraries"];
    NSMutableArray* libs = [NSMutableArray array];
    for (const Settings::LibraryDefinitionPtr& l : _defs->Libraries)
        [libs addObject:GRNS(l->Id + "  -  " + l->Name + "  (" + l->Kind + ", " + l->Target + ")")];
    _libBox = [form addPopup:@"Library" items:libs selected:nil];
    _libBox.target = self;
    _libBox.action = @selector(libChosen:);
    _libDesc = [form addNote:@""];
    _libState = [form addText:@"Source" value:@""];
    _libState.editable = NO;
    _libState.bezeled = NO;
    _libState.drawsBackground = NO;
    _libDir = [form addMono:@"Directory" value:@""];
    _libEnv = [form addMono:@"Directory variable" value:@""];
    _libMarker = [form addMono:@"Marker file" value:@""];
    _libDebug = [form addMono:@"Debug file(s)" value:@""];
    _libRelease = [form addMono:@"Release file(s)" value:@""];
    _libAliases = [form addMono:@"Project aliases (;)" value:@""];
    _libAsmDebug = [form addMono:@"Debug assembly directory" value:@""];
    _libAsmRelease = [form addMono:@"Release assembly directory" value:@""];
    _libWhen = [form addMono:@"Linked when" value:@""];
    [form addRow:@"" view:[NSButton buttonWithTitle:@"Restore shipped defaults (this library)" target:self action:@selector(restoreLib:)]];

    [self toolChosen:nil];
    [self libChosen:nil];
    return form.view;
}

static std::string SelectedId(NSPopUpButton* box)
{
    std::string s = GRStd(box.titleOfSelectedItem);
    size_t at = s.find("  -  ");
    return at == std::string::npos ? s : s.substr(0, at);
}

- (JsonPtr)viewOf:(const std::string&)section id:(const std::string&)ident
{
    std::map<std::string, JsonPtr>& map = section == "tools" ? _toolView : _libView;
    std::map<std::string, JsonPtr>::iterator it = map.find(ident);
    if (it != map.end()) return it->second;
    JsonPtr m = _defs->MergedEntry(section, ident);
    JsonPtr v = m ? ToolchainDefinitions::Clone(m) : JsonValue::MakeObject();
    map[ident] = v;
    return v;
}

- (NSString*)stateOf:(const std::string&)section id:(const std::string&)ident
{
    if (_touched.count(section + ":" + ident)) return @"changed here (not saved yet)";
    if (_defs->IsOverridden(section, ident)) return @"your file overrides the shipped entry";
    return @"as shipped";
}

// ---- tools ---------------------------------------------------------------------
- (void)toolChosen:(id)sender
{
    (void)sender;
    [self captureTool];
    _currentTool = SelectedId(_toolBox);
    [self showTool];
}

- (void)showTool
{
    if (_currentTool.empty()) return;
    JsonPtr v = [self viewOf:"tools" id:_currentTool];
    _toolDesc.stringValue = GRNS(v->GetString("description", ""));
    _toolState.stringValue = [self stateOf:"tools" id:_currentTool];
    _exe.stringValue = GRNS(v->GetString("exe", ""));
    _aliases.stringValue = Joined(v, "aliases");
    _words.stringValue = Joined(v, "words");
    _envVar.stringValue = Nested(v, "lookup", "env");
    _installDir.stringValue = Nested(v, "lookup", "installDir");
    _onPath.state = NestedBool(v, "lookup", "path", true) ? NSControlStateValueOn : NSControlStateValueOff;
    _vcvars.state = NestedBool(v, "lookup", "vcvars", false) ? NSControlStateValueOn : NSControlStateValueOff;
    _versionArgs.stringValue = Nested(v, "version", "args");
    _encoding.stringValue = GRNS(v->GetString("outputEncoding", ""));
    _diagnostics.stringValue = GRNS(v->GetString("diagnostics", ""));
    _archFlag.stringValue = GRNS(v->GetString("archFlag", ""));
    _includeFlag.stringValue = GRNS(v->GetString("includeFlag", ""));
    _defineFlag.stringValue = GRNS(v->GetString("defineFlag", ""));
    _installIncludes.stringValue = Joined(v, "installIncludeDirs");
    _debugFlags.stringValue = Nested(v, "configFlags", "Debug");
    _releaseFlags.stringValue = Nested(v, "configFlags", "Release");
    _codeviewFlag.stringValue = Nested(v, "asmSyntaxFlags", "codeview");
    _namedAsmFlag.stringValue = Nested(v, "asmSyntaxFlags", "assemblerNamed");
    [_ops clear];
    JsonPtr o = v->GetObject("operations");
    if (o)
        for (const std::string& op : o->KeyNames())
        {
            JsonPtr od = o->GetObject(op);
            if (!od) continue;
            [_ops addRow:@[GRNS(op), GRNS(od->GetString("args", "")), GRNS(od->GetString("description", ""))] tag:nil];
        }
    [_ops reload];
}

// the fields back into the view; a change marks the tool
- (void)captureTool
{
    if (_currentTool.empty()) return;
    JsonPtr v = [self viewOf:"tools" id:_currentTool];
    std::string before = JsonWriter::Write(v);
    v->SetString("exe", Trimmed(_exe));
    v->SetList("aliases", SplitEntries(_aliases.stringValue));
    v->SetList("words", SplitEntries(_words.stringValue));
    JsonPtr l = Sub(v, "lookup");
    l->SetString("env", Trimmed(_envVar));
    l->SetString("installDir", Trimmed(_installDir));
    l->SetBool("path", _onPath.state == NSControlStateValueOn);
    l->SetBool("vcvars", _vcvars.state == NSControlStateValueOn);
    Sub(v, "version")->SetString("args", Trimmed(_versionArgs));
    v->SetString("outputEncoding", Trimmed(_encoding));
    v->SetString("diagnostics", Trimmed(_diagnostics));
    v->SetString("archFlag", Trimmed(_archFlag));
    v->SetString("includeFlag", Trimmed(_includeFlag));
    v->SetString("defineFlag", Trimmed(_defineFlag));
    v->SetList("installIncludeDirs", SplitEntries(_installIncludes.stringValue));
    JsonPtr cf = Sub(v, "configFlags");
    cf->SetString("Debug", Trimmed(_debugFlags));
    cf->SetString("Release", Trimmed(_releaseFlags));
    JsonPtr af = Sub(v, "asmSyntaxFlags");
    af->SetString("codeview", Trimmed(_codeviewFlag));
    af->SetString("assemblerNamed", Trimmed(_namedAsmFlag));
    JsonPtr o = Sub(v, "operations");
    for (NSArray<NSString*>* row in _ops.rows)
    {
        std::string op = GRStd(row[0]);
        if (op.empty()) continue;
        Sub(o, op)->SetString("args", Core::StringUtil::Trim(GRStd(row[1])));
    }
    if (before != JsonWriter::Write(v)) _touched.insert("tools:" + _currentTool);
}

- (void)restoreTool:(id)sender
{
    (void)sender;
    if (_currentTool.empty()) return;
    JsonPtr shipped = _defs->ShippedEntry("tools", _currentTool);
    _toolView[_currentTool] = shipped ? ToolchainDefinitions::Clone(shipped) : JsonValue::MakeObject();
    _touched.insert("tools:" + _currentTool);
    [self showTool];
    _toolState.stringValue = @"shipped defaults (saved with OK)";
}

// ---- libraries -------------------------------------------------------------------
- (void)libChosen:(id)sender
{
    (void)sender;
    [self captureLib];
    _currentLib = SelectedId(_libBox);
    [self showLib];
}

- (void)showLib
{
    if (_currentLib.empty()) return;
    JsonPtr v = [self viewOf:"libraries" id:_currentLib];
    _libDesc.stringValue = GRNS(v->GetString("description", ""));
    _libState.stringValue = [self stateOf:"libraries" id:_currentLib];
    _libDir.stringValue = GRNS(v->GetString("directory", ""));
    _libEnv.stringValue = GRNS(v->GetString("directoryEnv", ""));
    _libMarker.stringValue = GRNS(v->GetString("marker", ""));
    _libDebug.stringValue = Nested(v, "files", "Debug");
    _libRelease.stringValue = Nested(v, "files", "Release");
    _libAliases.stringValue = Joined(v, "aliases");
    _libAsmDebug.stringValue = Nested(v, "assemblyDirectories", "Debug");
    _libAsmRelease.stringValue = Nested(v, "assemblyDirectories", "Release");
    _libWhen.stringValue = GRNS(v->GetString("when", ""));
}

- (void)captureLib
{
    if (_currentLib.empty()) return;
    JsonPtr v = [self viewOf:"libraries" id:_currentLib];
    std::string before = JsonWriter::Write(v);
    v->SetString("directory", Trimmed(_libDir));
    v->SetString("directoryEnv", Trimmed(_libEnv));
    v->SetString("marker", Trimmed(_libMarker));
    JsonPtr f = Sub(v, "files");
    f->SetString("Debug", Trimmed(_libDebug));
    f->SetString("Release", Trimmed(_libRelease));
    v->SetList("aliases", SplitEntries(_libAliases.stringValue));
    if (!Trimmed(_libAsmDebug).empty() || !Trimmed(_libAsmRelease).empty() || v->Has("assemblyDirectories"))
    {
        JsonPtr a = Sub(v, "assemblyDirectories");
        a->SetString("Debug", Trimmed(_libAsmDebug));
        a->SetString("Release", Trimmed(_libAsmRelease));
    }
    v->SetString("when", Trimmed(_libWhen));
    if (before != JsonWriter::Write(v)) _touched.insert("libraries:" + _currentLib);
}

- (void)restoreLib:(id)sender
{
    (void)sender;
    if (_currentLib.empty()) return;
    JsonPtr shipped = _defs->ShippedEntry("libraries", _currentLib);
    _libView[_currentLib] = shipped ? ToolchainDefinitions::Clone(shipped) : JsonValue::MakeObject();
    _touched.insert("libraries:" + _currentLib);
    [self showLib];
    _libState.stringValue = @"shipped defaults (saved with OK)";
}

// ---- OK ------------------------------------------------------------------------
- (void)commit
{
    [self captureTool];
    [self captureLib];
    if (_touched.empty()) return;
    for (const std::string& key : _touched)
    {
        size_t colon = key.find(':');
        std::string section = key.substr(0, colon);
        std::string ident = key.substr(colon + 1);
        JsonPtr shipped = _defs->ShippedEntry(section, ident);
        JsonPtr view = section == "tools" ? _toolView[ident] : _libView[ident];
        JsonPtr ov = _defs->IsOverridden(section, ident) ? ToolchainDefinitions::Clone(_defs->UserEntry(section, ident))
                                                         : JsonValue::MakeObject();
        if (section == "tools") ApplyToolDiff(ov, view, shipped);
        else ApplyLibDiff(ov, view, shipped);
        _defs->SetUserEntry(section, ident, ov);
    }
    std::string err;
    if (!_defs->SaveUser(err))
    {
        GRShowMessage(self.view.window, @"GRIDEm", [@"The toolchain overrides could not be saved:\n" stringByAppendingString:GRNS(err)]);
        return;
    }
    // the settings' defaults follow the new definitions; what the user set stays
    std::map<Core::Target, Settings::ToolchainSettingsPtr> before;
    for (Core::Target t : Core::TargetInfo::All()) before[t] = std::make_shared<Settings::ToolchainSettings>(t);
    ToolchainDefinitions::Reload();
    for (const Settings::ToolchainSettingsPtr& tc : self.work->Toolchains) tc->Rebase(before[tc->TargetKind]);
    _defs = ToolchainDefinitions::Current();
    _touched.clear();
}
@end
