// =============================================================================
//  ProjectTemplates.cpp - see ProjectTemplates.h
// =============================================================================
#include "ProjectTemplates.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Paths.h"
#include <algorithm>

namespace GRIDEm { namespace Services {

    using Core::Language;
    using Core::StringUtil;
    using Platform::Paths;

    std::vector<std::string> ProjectTemplates::For(Language l)
    {
        std::vector<std::string> r;
        if (l == Language::Shalimar)
        {
            r.push_back("Shalimar Program");
            r.push_back("Shalimar Module");
            r.push_back("From C Source");
            r.push_back("Empty Project");
            return r;
        }
        r.push_back("Console Application");
        r.push_back("DSP Application (C6747)");
        r.push_back("Static Library");
        r.push_back("Empty Project");
        return r;
    }

    std::string ProjectTemplates::Describe(const std::string& t)
    {
        if (t == "Console Application") return "Host program for Windows / Linux / macOS.";
        if (t == "DSP Application (C6747)") return "Bare-metal main + RTS6747 and a C6747 linker command file.";
        if (t == "Static Library") return "Reusable library; objects are kept, no link step.";
        if (t == "Shalimar Program") return "Shalimar micro-compiler program for any target.";
        if (t == "Shalimar Module") return "Reusable Shalimar unit.";
        if (t == "From C Source") return "Start empty, then use Tools > Convert > C to Shalimar.";
        return "Only the .pro file and the src / inc / lib folders.";
    }

    std::vector<ItemTemplate> ProjectTemplates::Items()
    {
        std::vector<ItemTemplate> r;
        r.push_back(ItemTemplate("C90 source file", ".c", "src"));
        r.push_back(ItemTemplate("C90 header file", ".h", "inc"));
        r.push_back(ItemTemplate("C++11 source file", ".cpp", "src"));
        r.push_back(ItemTemplate("C++11 header file", ".hpp", "inc"));
        r.push_back(ItemTemplate("Shalimar source file", ".shl", "src"));
        r.push_back(ItemTemplate("Assembly file", ".asm", "src"));
        r.push_back(ItemTemplate("Linker command file", ".cmd", "linker"));
        r.push_back(ItemTemplate("Text file", ".txt", "doc"));
        return r;
    }

    void ProjectTemplates::Populate(const Model::GrideProjectPtr& p)
    {
        std::string t = p->Template;
        std::string dir = p->ProjectDir();
        if (t == "Empty Project" || t == "From C Source") { p->Save(); return; }

        if (p->Lang == Language::Shalimar)
        {
            std::string name = "main.shl";
            if (t == "Shalimar Module") name = "module.shl";
            Write(p, Paths::Combine(dir, "src/" + name), ShalimarStarter(p->Name, t == "Shalimar Module"), "src");
        }
        else
        {
            bool cpp = p->Lang == Language::Cpp11;
            std::string hdrExt = cpp ? ".hpp" : ".h";
            std::string srcExt = cpp ? ".cpp" : ".c";
            std::string baseName = StringUtil::ToLower(p->Name);
            Write(p, Paths::Combine(dir, "inc/" + baseName + hdrExt), Header(p->Name), "inc");
            if (t == "Static Library")
                Write(p, Paths::Combine(dir, "src/" + baseName + srcExt), LibrarySource(p->Name, baseName + hdrExt), "src");
            else
                Write(p, Paths::Combine(dir, "src/main" + srcExt), MainSource(p, t == "DSP Application (C6747)", baseName + hdrExt), "src");
            bool c6747 = std::find(p->Targets.begin(), p->Targets.end(), Core::Target::C6747) != p->Targets.end();
            if (t == "DSP Application (C6747)" || c6747)
                Write(p, Paths::Combine(dir, "c6747.cmd"), LinkerCmd(), "linker");
        }
        p->Save();
    }

    // Regex::Replace(name.ToUpperInvariant(), "[^A-Z0-9]", "_") + "_H"
    std::string ProjectTemplates::Guard(const std::string& name)
    {
        std::string g = StringUtil::ToUpper(name);
        for (char& c : g)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
        return g + "_H";
    }

    std::string ProjectTemplates::ItemContent(const std::string& fileName, Language projectLang)
    {
        (void)projectLang;
        std::string ext = StringUtil::ToLower(Paths::Extension(fileName));
        std::string stem = Paths::FileNameWithoutExtension(fileName);
        std::string guard = Guard(stem);
        if (ext == ".h" || ext == ".hpp")
            return "#ifndef " + guard + "\r\n#define " + guard + "\r\n\r\n\r\n#endif /* " + guard + " */\r\n";
        if (ext == ".c")   return "/* " + fileName + " */\r\n\r\n";
        if (ext == ".cpp") return "// " + fileName + "\r\n\r\n";
        if (ext == ".shl") return "// " + fileName + " - Shalimar source\r\n\r\n";
        if (ext == ".asm") return "; " + fileName + "\r\n\r\n        .text\r\n\r\n";
        if (ext == ".cmd") return LinkerCmd();
        return std::string();
    }

    void ProjectTemplates::Write(const Model::GrideProjectPtr& p, const std::string& path, const std::string& content,
                                 const std::string& folder)
    {
        Core::PathUtil::EnsureDirectory(Paths::DirectoryName(path));
        if (!Paths::Exists(path))
        {
            std::string error;
            Paths::WriteFile(path, content, error);
        }
        p->AddFile(path, folder);
    }

    std::string ProjectTemplates::Header(const std::string& name)
    {
        std::string g = Guard(name);
        return "#ifndef " + g + "\r\n#define " + g + "\r\n\r\n/* " + name + " public interface */\r\n\r\n#endif /* " + g + " */\r\n";
    }

    std::string ProjectTemplates::MainSource(const Model::GrideProjectPtr& p, bool dsp, const std::string& header)
    {
        bool cpp = p->Lang == Language::Cpp11;
        std::string sb;
        if (cpp) sb += "// main.cpp - " + p->Name + " (C++11)\r\n";
        else sb += "/* main.c - " + p->Name + " (C90) */\r\n";
        sb += "#include \"" + header + "\"\r\n";
        if (!dsp) sb += "#include <stdio.h>\r\n";
        sb += "\r\n";
        if (dsp)
        {
            sb += "int main(void)\r\n{\r\n";
            if (cpp) sb += "    // device initialisation goes here\r\n";
            else sb += "    /* device initialisation goes here */\r\n";
            sb += "    for (;;)\r\n    {\r\n    }\r\n}\r\n";
        }
        else
        {
            sb += "int main(void)\r\n{\r\n";
            sb += "    printf(\"Hello from " + p->Name + "\\n\");\r\n";
            sb += "    return 0;\r\n}\r\n";
        }
        return sb;
    }

    std::string ProjectTemplates::LibrarySource(const std::string& name, const std::string& header)
    {
        return "/* " + name + " library */\r\n#include \"" + header + "\"\r\n\r\n";
    }

    std::string ProjectTemplates::ShalimarStarter(const std::string& name, bool module)
    {
        std::string kind = module ? "module" : "program";
        return "// " + name + " - Shalimar " + kind + "\r\n// [write Shalimar code here]\r\n\r\n";
    }

    // C6747: 256 KB L2 RAM at 0x11800000, 128 KB shared RAM at 0x80000000,
    // external SDRAM at 0xC0000000. Adjust to your board.
    std::string ProjectTemplates::LinkerCmd()
    {
        return
            "/* c6747.cmd - linker command file for TMS320C6747 (adjust to your board) */\r\n"
            "-stack 0x2000\r\n"
            "-heap  0x2000\r\n\r\n"
            "MEMORY\r\n{\r\n"
            "    L2RAM:   o = 0x11800000  l = 0x00040000   /* 256 KB internal L2 */\r\n"
            "    SHRAM:   o = 0x80000000  l = 0x00020000   /* 128 KB shared RAM  */\r\n"
            "    SDRAM:   o = 0xC0000000  l = 0x04000000   /* external SDRAM     */\r\n"
            "}\r\n\r\n"
            "SECTIONS\r\n{\r\n"
            "    .vectors  > L2RAM\r\n"
            "    .text     > L2RAM\r\n"
            "    .stack    > L2RAM\r\n"
            "    .bss      > L2RAM\r\n"
            "    .cinit    > L2RAM\r\n"
            "    .const    > L2RAM\r\n"
            "    .data     > L2RAM\r\n"
            "    .switch   > L2RAM\r\n"
            "    .sysmem   > SDRAM\r\n"
            "    .far      > SDRAM\r\n"
            "    .cio      > L2RAM\r\n"
            "}\r\n";
    }
}}
