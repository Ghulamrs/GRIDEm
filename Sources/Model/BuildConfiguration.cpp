// =============================================================================
//  BuildConfiguration.cpp - see BuildConfiguration.h
// =============================================================================
#include "BuildConfiguration.h"
#include "../Core/StringUtil.h"

namespace GRIDEm { namespace Model {

    using namespace GRIDEm::Core;

    BuildConfiguration::BuildConfiguration(const std::string& config, Target target)
        : Config(config), TargetKind(target)
    {
        ApplyDefaults();
    }

    std::string BuildConfiguration::Key() const { return ConfigName::Make(Config, TargetKind); }

    bool BuildConfiguration::IsDebug() const { return StringUtil::EqualsIgnoreCase(Config, "Debug"); }

    void BuildConfiguration::ApplyDefaults()
    {
        std::string folder = TargetInfo::FolderName(TargetKind);
        OutDir = "out/" + Config + "/" + folder;
        ObjDir = "obj/" + Config + "/" + folder;
        Defines.clear();
        if (IsDebug()) { Defines.push_back("DEBUG"); Optimise = "O0"; }
        else           { Defines.push_back("NDEBUG"); Optimise = "O2"; }
        IncludeDirs.clear();
        IncludeDirs.push_back("inc");
        LibDirs.clear();
        Libs.clear();
        if (TargetKind == Target::C6747)
        {
            // RTS6x's library; rts6x.lib, rts6xd.lib, rts6747.lib and libc.a are all read as the
            // configuration's one (rts6xd.lib for Debug) when the program is linked
            LinkerCmd = "c6747.cmd";
            Libs.push_back("rts6x.lib");
        }
        else LinkerCmd.clear();
        ExtraCompileFlags.clear();
        ExtraLinkFlags.clear();
        OutputName.clear();
    }

    BuildConfigurationPtr BuildConfiguration::Clone() const
    {
        return std::make_shared<BuildConfiguration>(*this);
    }

    JsonPtr BuildConfiguration::ToJson() const
    {
        JsonPtr o = JsonValue::MakeObject();
        o->SetString("outDir", OutDir);
        o->SetString("objDir", ObjDir);
        o->SetList("defines", Defines);
        o->SetList("includeDirs", IncludeDirs);
        o->SetList("libDirs", LibDirs);
        o->SetList("libs", Libs);
        o->SetString("optimise", Optimise);
        if (!LinkerCmd.empty()) o->SetString("linkerCmd", LinkerCmd);
        if (!ExtraCompileFlags.empty()) o->SetString("compileFlags", ExtraCompileFlags);
        if (!ExtraLinkFlags.empty()) o->SetString("linkFlags", ExtraLinkFlags);
        if (!OutputName.empty()) o->SetString("outputName", OutputName);
        return o;
    }

    BuildConfigurationPtr BuildConfiguration::FromJson(const std::string& key, const JsonPtr& o)
    {
        BuildConfigurationPtr c = std::make_shared<BuildConfiguration>(ConfigName::ConfigPart(key),
                                                                       TargetInfo::Parse(ConfigName::TargetPart(key)));
        if (!o || !o->IsObject()) return c;
        c->OutDir = o->GetString("outDir", c->OutDir);
        c->ObjDir = o->GetString("objDir", c->ObjDir);
        if (o->Has("defines"))     c->Defines = o->GetStringList("defines");
        if (o->Has("includeDirs")) c->IncludeDirs = o->GetStringList("includeDirs");
        if (o->Has("libDirs"))     c->LibDirs = o->GetStringList("libDirs");
        if (o->Has("libs"))        c->Libs = o->GetStringList("libs");
        c->Optimise = o->GetString("optimise", c->Optimise);
        c->LinkerCmd = o->GetString("linkerCmd", c->LinkerCmd);
        c->ExtraCompileFlags = o->GetString("compileFlags", "");
        c->ExtraLinkFlags = o->GetString("linkFlags", "");
        c->OutputName = o->GetString("outputName", "");
        return c;
    }
}}
