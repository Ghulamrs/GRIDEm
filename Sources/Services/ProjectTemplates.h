// =============================================================================
//  ProjectTemplates.h - starter files for File > New > Project and
//  Project > Add New Item. Content is plain text; edit freely.
//  (GRIDEd's Services/ProjectTemplates.h: ItemTemplate, ProjectTemplates)
// =============================================================================
#pragma once
#include "../Core/LanguageTarget.h"
#include "../Model/GrideProject.h"
#include <string>
#include <vector>

namespace GRIDEm { namespace Services {

    class ItemTemplate
    {
    public:
        ItemTemplate(const std::string& name, const std::string& ext, const std::string& folder)
            : Name(name), Extension(ext), Folder(folder) {}
        std::string Name;
        std::string Extension;
        std::string Folder;
        std::string ToString() const { return Name + "  (" + Extension + ")"; }
    };

    class ProjectTemplates
    {
    public:
        // template names shown in the New Project dialog, per language
        static std::vector<std::string> For(Core::Language l);
        static std::string Describe(const std::string& t);
        static std::vector<ItemTemplate> Items();
        // writes the starter files of a new project and adds them to it
        static void Populate(const Model::GrideProjectPtr& p);
        // starter content for Project > Add New Item
        static std::string ItemContent(const std::string& fileName, Core::Language projectLang);
        // the C6747 linker command file of a new project
        static std::string LinkerCmd();

    private:
        static void Write(const Model::GrideProjectPtr& p, const std::string& path, const std::string& content,
                          const std::string& folder);
        static std::string Guard(const std::string& name);
        static std::string Header(const std::string& name);
        static std::string MainSource(const Model::GrideProjectPtr& p, bool dsp, const std::string& header);
        static std::string LibrarySource(const std::string& name, const std::string& header);
        static std::string ShalimarStarter(const std::string& name, bool module);
    };
}}
