// ProjectReader.cpp - reads .sln, .vcxproj and .vcproj files.
#include "ProjectReader.hpp"
#include "PathUtil.hpp"

#include <cctype>
#include <sstream>

namespace cppparse {

// ---- mini XML ---------------------------------------------------------------

std::string XmlElement::attr(const std::string& key) const
{
    for (size_t i = 0; i < attrs.size(); ++i)
        if (iequals(attrs[i].first, key)) return attrs[i].second;
    return "";
}

void findElements(const std::string& xml, const std::string& tag, std::vector<XmlElement>& out)
{
    const size_t n = xml.size();
    size_t pos = 0;
    const std::string open = "<" + tag;
    for (;;) {
        size_t hit = xml.find(open, pos);
        if (hit == std::string::npos) return;
        pos = hit + 1;
        // skip comments that happen to contain the tag
        size_t cstart = xml.rfind("<!--", hit);
        if (cstart != std::string::npos) {
            size_t cend = xml.find("-->", cstart);
            if (cend != std::string::npos && cend > hit) { pos = cend + 3; continue; }
        }
        size_t i = hit + open.size();
        if (i >= n) return;
        char c = xml[i];
        if (!(isspace(static_cast<unsigned char>(c)) || c == '/' || c == '>')) continue; // longer tag name

        XmlElement e;
        e.name = tag;
        // attributes
        for (;;) {
            while (i < n && isspace(static_cast<unsigned char>(xml[i]))) ++i;
            if (i >= n) return;
            if (xml[i] == '/') { e.selfClosing = true; ++i; continue; }
            if (xml[i] == '>') { ++i; break; }
            size_t ks = i;
            while (i < n && xml[i] != '=' && xml[i] != '>' && !isspace(static_cast<unsigned char>(xml[i]))) ++i;
            std::string key = xml.substr(ks, i - ks);
            while (i < n && isspace(static_cast<unsigned char>(xml[i]))) ++i;
            std::string value;
            if (i < n && xml[i] == '=') {
                ++i;
                while (i < n && isspace(static_cast<unsigned char>(xml[i]))) ++i;
                if (i < n && (xml[i] == '"' || xml[i] == '\'')) {
                    char q = xml[i++];
                    size_t vs = i;
                    while (i < n && xml[i] != q) ++i;
                    value = xmlDecode(xml.substr(vs, i - vs));
                    if (i < n) ++i;
                } else {
                    size_t vs = i;
                    while (i < n && !isspace(static_cast<unsigned char>(xml[i])) && xml[i] != '>') ++i;
                    value = xmlDecode(xml.substr(vs, i - vs));
                }
            }
            if (!key.empty()) e.attrs.push_back(std::make_pair(key, value));
        }
        if (!e.selfClosing) {
            std::string close = "</" + tag + ">";
            size_t end = xml.find(close, i);
            if (end == std::string::npos) end = n;
            e.content = xml.substr(i, end - i);
            pos = end;
        } else {
            pos = i;
        }
        out.push_back(e);
    }
}

// ---- helpers ----------------------------------------------------------------

std::string ProjectReader::describeToolsVersion(const std::string& toolsVersion, const std::string& toolset)
{
    std::string vs;
    std::string ts = toLower(toolset);
    if      (ts == "v90")  vs = "Visual Studio 2008";
    else if (ts == "v100") vs = "Visual Studio 2010";
    else if (ts == "v110") vs = "Visual Studio 2012";
    else if (ts == "v120") vs = "Visual Studio 2013";
    else if (ts == "v140") vs = "Visual Studio 2015";
    else if (ts == "v141") vs = "Visual Studio 2017";
    else if (ts == "v142") vs = "Visual Studio 2019";
    else if (ts == "v143") vs = "Visual Studio 2022";
    else if (ts == "v145") vs = "Visual Studio 2026";
    if (vs.empty()) {
        if      (toolsVersion == "4.0")  vs = "Visual Studio 2010/2012";
        else if (toolsVersion == "12.0") vs = "Visual Studio 2013";
        else if (toolsVersion == "14.0") vs = "Visual Studio 2015";
        else if (toolsVersion == "15.0") vs = "Visual Studio 2017";
        else if (toolsVersion == "Current") vs = "Visual Studio 2019 or later";
        else vs = "Visual Studio (unknown version)";
    }
    std::string out = vs;
    if (!toolsVersion.empty()) out += ", ToolsVersion " + toolsVersion;
    if (!toolset.empty()) out += ", toolset " + toolset;
    return out;
}

std::string ProjectReader::expandMacros(const std::string& s, const ProjectInfo& p, const std::string& solutionDir)
{
    std::string projDir = dirName(p.path);
    std::string out = replaceAll(s, "$(ProjectDir)", projDir + "/");
    out = replaceAll(out, "$(MSBuildProjectDirectory)", projDir);
    out = replaceAll(out, "$(ProjectName)", p.name);
    if (!solutionDir.empty()) out = replaceAll(out, "$(SolutionDir)", solutionDir + "/");
    return out;
}

void ProjectReader::addList(std::vector<std::string>& target, const std::string& list, const ProjectInfo& p,
                            const std::string& solutionDir, bool asPaths)
{
    std::vector<std::string> items;
    split(list, ';', items);
    for (size_t i = 0; i < items.size(); ++i) {
        std::string it = trim(items[i]);
        if (it.empty() || startsWith(it, "%(")) continue;
        if (asPaths) {
            it = expandMacros(it, p, solutionDir);
            if (it.find("$(") != std::string::npos) continue; // unresolved macro
            it = joinPath(dirName(p.path), it);
        }
        addUnique(target, it);
    }
}

void ProjectReader::addFile(ProjectInfo& out, const std::string& relative)
{
    std::string rel = trim(relative);
    if (rel.empty()) return;
    std::string abs = joinPath(dirName(out.path), expandMacros(rel, out, ""));
    std::string ext = extension(abs);
    if (isHeaderExtension(ext)) addUnique(out.headerFiles, abs);
    else if (isSourceExtension(ext)) addUnique(out.sourceFiles, abs);
    else addUnique(out.otherFiles, abs);
}

// ---- solution -----------------------------------------------------------------

bool ProjectReader::readSolution(const std::string& path, SolutionInfo& out)
{
    std::string text;
    out.path = absolutePath(path);
    if (!readFile(out.path, text)) return false;
    std::string dir = dirName(out.path);

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (startsWith(line, "Microsoft Visual Studio Solution File")) {
            out.version = line.substr(std::string("Microsoft Visual Studio Solution File, ").size());
            continue;
        }
        if (startsWith(line, "# Visual Studio")) {
            out.version += (out.version.empty() ? "" : "; ") + line.substr(2);
            continue;
        }
        if (startsWith(line, "VisualStudioVersion")) {
            out.version += (out.version.empty() ? "" : "; ") + line;
            continue;
        }
        if (startsWith(line, "Project(")) {
            // Project("{GUID}") = "Name", "relative\path.vcxproj", "{GUID}"
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::vector<std::string> quoted;
            size_t i = eq;
            while (i < line.size()) {
                size_t q1 = line.find('"', i);
                if (q1 == std::string::npos) break;
                size_t q2 = line.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                quoted.push_back(line.substr(q1 + 1, q2 - q1 - 1));
                i = q2 + 1;
            }
            if (quoted.size() < 2) continue;
            std::string rel = quoted[1];
            std::string ext = extension(rel);
            if (ext == ".vcxproj" || ext == ".vcproj") {
                out.projectPaths.push_back(joinPath(dir, rel));
            } else {
                out.warnings.push_back("skipped non-C++ project \"" + quoted[0] + "\" (" + rel + ")");
            }
        }
    }
    return true;
}

// ---- project ----------------------------------------------------------------

bool ProjectReader::readProject(const std::string& path, ProjectInfo& out, const std::string& solutionDir)
{
    std::string xml;
    out.path = absolutePath(path);
    if (!readFile(out.path, xml)) return false;
    std::string base = baseName(out.path);
    size_t dot = base.rfind('.');
    out.name = dot == std::string::npos ? base : base.substr(0, dot);
    std::string ext = extension(out.path);
    if (ext == ".vcxproj") { out.kind = "vcxproj"; readVcxproj(xml, out, solutionDir); }
    else if (ext == ".vcproj") { out.kind = "vcproj"; readVcproj(xml, out, solutionDir); }
    else { out.kind = "unknown"; out.warnings.push_back("unknown project type: " + ext); return false; }
    return true;
}

void ProjectReader::readVcxproj(const std::string& xml, ProjectInfo& out, const std::string& solutionDir)
{
    std::vector<XmlElement> els;

    findElements(xml, "Project", els);
    std::string toolsVersion;
    if (!els.empty()) toolsVersion = els[0].attr("ToolsVersion");
    els.clear();

    findElements(xml, "PlatformToolset", els);
    if (!els.empty()) out.toolset = trim(els[0].content);
    els.clear();
    out.version = describeToolsVersion(toolsVersion, out.toolset);

    findElements(xml, "ProjectName", els);
    if (!els.empty() && !trim(els[0].content).empty()) out.name = trim(els[0].content);
    els.clear();
    if (out.name.empty()) {
        findElements(xml, "RootNamespace", els);
        if (!els.empty()) out.name = trim(els[0].content);
        els.clear();
    }

    findElements(xml, "ProjectConfiguration", els);
    for (size_t i = 0; i < els.size(); ++i) addUnique(out.configurations, els[i].attr("Include"));
    els.clear();

    findElements(xml, "ClCompile", els);
    for (size_t i = 0; i < els.size(); ++i) { std::string inc = els[i].attr("Include"); if (!inc.empty()) addFile(out, inc); }
    els.clear();
    findElements(xml, "ClInclude", els);
    for (size_t i = 0; i < els.size(); ++i) { std::string inc = els[i].attr("Include"); if (!inc.empty()) addFile(out, inc); }
    els.clear();
    static const char* const otherTags[] = { "None", "ResourceCompile", "Text", "Image", "Midl", "CustomBuild", "Manifest", 0 };
    for (int t = 0; otherTags[t]; ++t) {
        findElements(xml, otherTags[t], els);
        for (size_t i = 0; i < els.size(); ++i) { std::string inc = els[i].attr("Include"); if (!inc.empty()) addFile(out, inc); }
        els.clear();
    }

    findElements(xml, "AdditionalIncludeDirectories", els);
    for (size_t i = 0; i < els.size(); ++i) addList(out.includeDirs, els[i].content, out, solutionDir, true);
    els.clear();
    findElements(xml, "PreprocessorDefinitions", els);
    for (size_t i = 0; i < els.size(); ++i) addList(out.preprocessorDefinitions, els[i].content, out, solutionDir, false);
    els.clear();

    findElements(xml, "Import", els);
    for (size_t i = 0; i < els.size(); ++i) {
        std::string p = els[i].attr("Project");
        if (endsWith(toLower(p), ".props") && p.find("$(") == std::string::npos)
            out.warnings.push_back("property sheet not evaluated: " + p);
    }
}

void ProjectReader::readVcproj(const std::string& xml, ProjectInfo& out, const std::string& solutionDir)
{
    std::vector<XmlElement> els;
    findElements(xml, "VisualStudioProject", els);
    std::string ver;
    if (!els.empty()) {
        ver = els[0].attr("Version");
        std::string nm = els[0].attr("Name");
        if (!nm.empty()) out.name = nm;
    }
    els.clear();
    if      (startsWith(ver, "9"))    out.version = "Visual Studio 2008";
    else if (startsWith(ver, "8"))    out.version = "Visual Studio 2005";
    else if (startsWith(ver, "7.1"))  out.version = "Visual Studio .NET 2003";
    else if (startsWith(ver, "7"))    out.version = "Visual Studio .NET 2002";
    else                              out.version = "Visual Studio (classic .vcproj)";
    if (!ver.empty()) out.version += ", format " + ver;

    findElements(xml, "Configuration", els);
    for (size_t i = 0; i < els.size(); ++i) { std::string nm = els[i].attr("Name"); if (!nm.empty()) addUnique(out.configurations, nm); }
    els.clear();

    findElements(xml, "Tool", els);
    for (size_t i = 0; i < els.size(); ++i) {
        if (!iequals(els[i].attr("Name"), "VCCLCompilerTool")) continue;
        addList(out.includeDirs, els[i].attr("AdditionalIncludeDirectories"), out, solutionDir, true);
        addList(out.preprocessorDefinitions, els[i].attr("PreprocessorDefinitions"), out, solutionDir, false);
    }
    els.clear();

    findElements(xml, "File", els);
    for (size_t i = 0; i < els.size(); ++i) addFile(out, els[i].attr("RelativePath"));
    els.clear();
}

} // namespace cppparse
