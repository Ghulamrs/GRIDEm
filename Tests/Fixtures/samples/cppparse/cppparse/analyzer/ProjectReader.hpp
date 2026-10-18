// ProjectReader.hpp - reads Visual Studio solution and C++ project files.
//
//   .sln      Visual Studio 2005 .. 2022 solutions (project list)
//   .vcxproj  Visual Studio 2010 and later (MSBuild XML)
//   .vcproj   Visual Studio 2005 / 2008 (classic XML)
//
// Only the information the parser needs is extracted: the list of header,
// source and other files, include directories, preprocessor definitions,
// configurations, and the Visual Studio version. A tiny XML scanner is
// included; no XML library is required.
//
// Strict C++98.
#ifndef CPPPARSE_PROJECTREADER_HPP
#define CPPPARSE_PROJECTREADER_HPP

#include "Model.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cppparse {

struct XmlElement {
    std::string name;
    std::vector<std::pair<std::string, std::string> > attrs;
    std::string content;       // text between start and end tag (raw)
    bool        selfClosing;
    XmlElement() : selfClosing(false) {}
    std::string attr(const std::string& key) const;
};

// Finds every element named 'tag' (no nesting of the same tag assumed).
void findElements(const std::string& xml, const std::string& tag, std::vector<XmlElement>& out);

class ProjectReader {
public:
    static bool readSolution(const std::string& path, SolutionInfo& out);
    static bool readProject(const std::string& path, ProjectInfo& out, const std::string& solutionDir);
    static std::string describeToolsVersion(const std::string& toolsVersion, const std::string& toolset);

private:
    static void readVcxproj(const std::string& xml, ProjectInfo& out, const std::string& solutionDir);
    static void readVcproj(const std::string& xml, ProjectInfo& out, const std::string& solutionDir);
    static void addFile(ProjectInfo& out, const std::string& relative);
    static std::string expandMacros(const std::string& s, const ProjectInfo& p, const std::string& solutionDir);
    static void addList(std::vector<std::string>& target, const std::string& list, const ProjectInfo& p,
                        const std::string& solutionDir, bool asPaths);
};

} // namespace cppparse

#endif
