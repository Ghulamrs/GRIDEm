// Html.cpp - self-contained HTML report.
#include "Html.hpp"
#include "Health.hpp"
#include "PathUtil.hpp"

#include <sstream>

namespace cppparse {

namespace {

std::string h(const std::string& s) { return xmlEscape(s); }

const char* kCss =
    "body{font-family:'Segoe UI',Arial,sans-serif;margin:0;background:#f7f8fa;color:#222}"
    "header{background:#1f3a5f;color:#fff;padding:18px 28px}header h1{margin:0 0 6px 0;font-size:22px}header .dim{color:#cfd8e6}"
    "nav{position:sticky;top:0;background:#fff;border-bottom:1px solid #ddd;padding:8px 28px;z-index:2;font-size:14px}nav a{margin-right:16px;color:#1f3a5f;text-decoration:none}nav a:hover{text-decoration:underline}"
    "main{padding:20px 28px;max-width:1600px}section{margin-bottom:36px}h2{border-bottom:2px solid #1f3a5f;padding-bottom:4px;margin-top:30px}h3{margin:18px 0 6px 0}"
    "table{border-collapse:collapse;width:100%;background:#fff;font-size:13px;margin:6px 0}th,td{border:1px solid #ddd;padding:4px 8px;vertical-align:top;text-align:left}th{background:#eef2f7}"
    "tr.sec td{background:#f3f5f9;font-weight:600}"
    "code,.mono{font-family:Consolas,'Courier New',monospace;font-size:12.5px;white-space:pre-wrap}"
    "details{background:#fff;border:1px solid #ddd;border-radius:4px;margin:8px 0;padding:6px 10px}summary{cursor:pointer;font-weight:600}details.cls>summary{font-size:15px}"
    ".cards{display:flex;flex-wrap:wrap;gap:12px}.card{background:#fff;border:1px solid #ddd;border-radius:6px;padding:10px 16px;min-width:150px}.card b{display:block;font-size:24px;color:#1f3a5f}.card span{font-size:12px;color:#555}"
    ".diagram{overflow:auto;background:#fff;border:1px solid #ddd;padding:4px}"
    ".warn{color:#b03030}.ok{color:#2a7a2a}.dim{color:#777}.tag{display:inline-block;background:#eef2f7;border-radius:3px;padding:0 6px;font-size:11px;margin-left:4px;color:#1f3a5f}"
    "pre{background:#fff;border:1px solid #ddd;padding:10px;overflow:auto;font-size:12px;font-family:Consolas,'Courier New',monospace}"
    "ul.plain{list-style:none;padding-left:0;margin:4px 0}ul.plain li{margin:2px 0}"
    "@media print{nav{display:none}details{page-break-inside:avoid}}";

std::string card(const std::string& label, int value)
{
    return "<div class=\"card\"><b>" + intToString(value) + "</b><span>" + h(label) + "</span></div>";
}

std::string functionRow(const CodeBase& cb, const FunctionInfo& fn, const ReportOptions& opt, bool showAccess)
{
    std::ostringstream s;
    s << "<tr>";
    if (showAccess) s << "<td>" << h(accessName(fn.access)) << "</td>";
    s << "<td><code>" << h(fn.signature()) << "</code>";
    if (fn.tmpl.isTemplate) s << "<br><span class=\"dim mono\">" << h(fn.tmpl.text()) << "</span>";
    if (!fn.condition.empty()) s << "<br><span class=\"dim\">#if: <code>" << h(fn.condition) << "</code></span>";
    s << "</td>";
    s << "<td>" << h(functionKind(fn)) << "</td>";
    if (fn.hasBody) {
        s << "<td>" << (fn.declaredAt.empty() ? std::string("<span class=\"dim\">(in place)</span>") : h(fn.declaredAt)) << "</td>";
        s << "<td>" << h(locationText(cb, fn.file, fn.line, fn.endLine)) << "</td>";
    } else {
        s << "<td>" << h(locationText(cb, fn.file, fn.line, 0)) << "</td>";
        if (!fn.definedAt.empty()) s << "<td>" << h(fn.definedAt) << "</td>";
        else if (fn.isPureVirtual) s << "<td><span class=\"dim\">pure virtual</span></td>";
        else s << "<td><span class=\"warn\">no definition found</span></td>";
    }
    s << "<td class=\"mono\">";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i) s << "<br>";
        s << h(fn.params[i].type + (fn.params[i].name.empty() ? "" : " " + fn.params[i].name) + (fn.params[i].defaultValue.empty() ? "" : " = " + fn.params[i].defaultValue));
    }
    s << "</td>";
    const FunctionInfo* body = bodyOf(fn);
    s << "<td class=\"mono\">";
    if (body && opt.locals) {
        if (body->hasInitializerList) s << "<span class=\"dim\">init: " << h(body->initializerList) << "</span><br>";
        for (size_t i = 0; i < body->locals.size(); ++i) {
            const LocalVariable& v = body->locals[i];
            s << h(declText(v.type, v.name) + (v.initializer.empty() ? "" : " = " + v.initializer)) << " <span class=\"dim\">L" << v.line << "</span><br>";
        }
        if (body->locals.empty()) s << "<span class=\"dim\">(none)</span>";
    }
    s << "</td>";
    s << "<td>" << (body ? h(exceptionSummary(body->exceptions)) : std::string("")) << (fn.linkNote.empty() ? "" : "<br><span class=\"dim\">" + h(fn.linkNote) + "</span>") << "</td>";
    s << "</tr>\n";
    return s.str();
}

const char* kFunctionHead =
    "<tr><th>Access</th><th>Signature</th><th>Kind</th><th>Declared</th><th>Defined</th><th>Parameters</th><th>Local variables</th><th>Exceptions / notes</th></tr>\n";
const char* kFunctionHeadNoAccess =
    "<tr><th>Signature</th><th>Kind</th><th>Declared</th><th>Defined</th><th>Parameters</th><th>Local variables</th><th>Exceptions / notes</th></tr>\n";

std::string classAnchor(const ClassInfo& c)
{
    std::string a = "cls-" + c.qualifiedName + c.tmpl.specializationArgs;
    for (size_t i = 0; i < a.size(); ++i) if (!isalnum(static_cast<unsigned char>(a[i]))) a[i] = '_';
    return a;
}

std::string classLink(const ClassInfo& c)
{
    return "<a href=\"#" + classAnchor(c) + "\">" + h(c.qualifiedName + c.tmpl.specializationArgs) + "</a>";
}

void writeClass(std::ostringstream& s, const CodeBase& cb, const ClassInfo& c, const ReportOptions& opt)
{
    s << "<details class=\"cls\" id=\"" << classAnchor(c) << "\"" << (c.isDefinition ? " open" : "") << "><summary>"
      << h(std::string(ClassInfo::kindName(c.kind)) + " " + c.qualifiedName + c.tmpl.specializationArgs);
    if (!c.bases.empty()) {
        s << " : ";
        for (size_t b = 0; b < c.bases.size(); ++b) {
            if (b) s << ", ";
            s << h(std::string(accessName(c.bases[b].access)) + (c.bases[b].isVirtual ? " virtual " : " "));
            if (c.bases[b].resolved) s << classLink(*c.bases[b].resolved); else s << h(c.bases[b].name) << "<span class=\"tag\">external</span>";
        }
    }
    if (c.tmpl.isTemplate) s << "<span class=\"tag\">template</span>";
    if (c.isAbstract) s << "<span class=\"tag\">abstract</span>";
    if (c.owner) s << "<span class=\"tag\">nested</span>";
    if (!c.isDefinition) s << "<span class=\"tag\">forward declaration</span>";
    s << "</summary>\n";

    s << "<table><tr><th style=\"width:160px\">Location</th><td>" << h(locationText(cb, c.file, c.line, c.endLine)) << "</td></tr>";
    if (c.tmpl.isTemplate) s << "<tr><th>Template</th><td><code>" << h(c.tmpl.text()) << "</code>" << (c.tmpl.specializationArgs.empty() ? "" : " specialization <code>" + h(c.tmpl.specializationArgs) + "</code>") << "</td></tr>";
    if (!c.attributes.empty()) s << "<tr><th>Attributes</th><td><code>" << h(c.attributes) << "</code></td></tr>";
    if (!c.condition.empty()) s << "<tr><th>Active #if</th><td><code>" << h(c.condition) << "</code></td></tr>";
    if (c.owner) s << "<tr><th>Nested in</th><td>" << classLink(*c.owner) << " (" << h(accessName(c.access)) << ")</td></tr>";
    if (!c.derived.empty()) {
        s << "<tr><th>Derived classes</th><td>";
        for (size_t i = 0; i < c.derived.size(); ++i) { if (i) s << ", "; s << classLink(*c.derived[i]); }
        s << "</td></tr>";
    }
    if (!c.associations.empty()) {
        s << "<tr><th>Uses (member types)</th><td>";
        for (size_t i = 0; i < c.associations.size(); ++i) { if (i) s << ", "; s << classLink(*c.associations[i]); }
        s << "</td></tr>";
    }
    if (!c.nested.empty()) {
        s << "<tr><th>Nested classes</th><td>";
        for (size_t i = 0; i < c.nested.size(); ++i) { if (i) s << ", "; s << classLink(*c.nested[i]); }
        s << "</td></tr>";
    }
    s << "</table>\n";
    if (!c.isDefinition) { s << "</details>\n"; return; }

    s << "<h3>Methods (" << c.methods.size() << ")</h3>\n";
    if (!c.methods.empty()) {
        s << "<table>" << kFunctionHead;
        for (size_t m = 0; m < c.methods.size(); ++m) s << functionRow(cb, c.methods[m], opt, true);
        s << "</table>\n";
    }
    s << "<h3>Fields (" << c.fields.size() << ")</h3>\n";
    if (!c.fields.empty()) {
        s << "<table><tr><th>Access</th><th>Declaration</th><th>Line</th><th>Notes</th></tr>\n";
        for (size_t f = 0; f < c.fields.size(); ++f) {
            const VariableInfo& v = c.fields[f];
            std::string decl = std::string(v.isStatic ? "static " : "") + (v.isMutable ? "mutable " : "") + declText(v.type, v.name);
            if (!v.bitField.empty()) decl += " : " + v.bitField;
            if (!v.initializer.empty()) decl += " = " + v.initializer;
            s << "<tr><td>" << h(accessName(v.access)) << "</td><td><code>" << h(decl) << "</code></td><td>L" << v.line << "</td><td>"
              << (v.definedAt.empty() ? "" : "defined " + h(v.definedAt)) << (v.condition.empty() ? "" : " #if: <code>" + h(v.condition) + "</code>") << "</td></tr>\n";
        }
        s << "</table>\n";
    }
    if (!c.typedefs.empty() || !c.enums.empty() || !c.friends.empty() || !c.usings.empty()) {
        s << "<h3>Typedefs, enums, friends</h3><ul class=\"plain mono\">";
        for (size_t i = 0; i < c.typedefs.size(); ++i) s << "<li>" << h(accessName(c.typedefs[i].access)) << ": " << h(c.typedefs[i].text) << " <span class=\"dim\">L" << c.typedefs[i].line << "</span></li>";
        for (size_t i = 0; i < c.enums.size(); ++i) {
            std::string e;
            for (size_t k = 0; k < c.enums[i].enumerators.size(); ++k) { if (k) e += ", "; e += c.enums[i].enumerators[k]; }
            s << "<li>" << h(accessName(c.enums[i].access)) << ": enum " << h(c.enums[i].name) << " { " << h(e) << " } <span class=\"dim\">L" << c.enums[i].line << "</span></li>";
        }
        for (size_t i = 0; i < c.friends.size(); ++i) s << "<li>friend " << h(c.friends[i].text) << " <span class=\"dim\">L" << c.friends[i].line << "</span></li>";
        for (size_t i = 0; i < c.usings.size(); ++i) s << "<li>" << h(c.usings[i].text) << " <span class=\"dim\">L" << c.usings[i].line << "</span></li>";
        s << "</ul>\n";
    }
    s << "</details>\n";
}

} // namespace

std::string renderHtmlReport(const CodeBase& cb, const DiagramLayout& layout, const ReportOptions& opt)
{
    std::ostringstream s;
    Totals t;
    computeTotals(cb, t);
    const std::vector<SourceFile*>& files = cb.files();
    const std::vector<ClassInfo*>& classes = cb.classes();

    s << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      << "<title>" << h(cb.title()) << " - cppparse report</title><style>" << kCss << "</style></head><body>\n";
    s << "<header><h1>" << h(cb.title()) << " &mdash; C++ project report</h1><div class=\"dim\">Generated by cppparse. Input: ";
    for (size_t i = 0; i < cb.inputs().size(); ++i) { if (i) s << ", "; s << h(cb.inputs()[i]); }
    s << ". Paths relative to " << h(cb.baseDirectory()) << "</div></header>\n";
    s << "<nav><a href=\"#summary\">Summary</a><a href=\"#project\">Project</a><a href=\"#diagram\">Class diagram</a><a href=\"#classes\">Classes</a>"
      << "<a href=\"#globals\">Globals</a><a href=\"#files\">Files</a><a href=\"#tree\">Inheritance</a><a href=\"#exceptions\">Exceptions</a><a href=\"#health\">Health</a><a href=\"#totals\">Totals</a></nav>\n<main>\n";

    // summary
    s << "<section id=\"summary\"><h2>Summary</h2><div class=\"cards\">"
      << card("files", t.files) << card("header files", t.headers) << card("source files", t.sources) << card("lines", t.lines)
      << card("classes / structs / unions", t.classes + t.structs + t.unions) << card("methods", t.methods) << card("free functions", t.freeFunctions)
      << card("global variables", t.globals) << card("local variables", t.locals) << card("macros", t.macros) << card("directives", t.directives)
      << card("try blocks", t.tryBlocks) << card("health errors", t.healthErrors) << card("health warnings", t.healthWarnings) << "</div></section>\n";

    // project
    s << "<section id=\"project\"><h2>Project</h2>";
    for (size_t i = 0; i < cb.solutions().size(); ++i)
        s << "<p><b>Solution:</b> " << h(cb.displayPath(cb.solutions()[i].path)) << " <span class=\"dim\">" << h(cb.solutions()[i].version) << "</span></p>";
    for (size_t p = 0; p < cb.projects().size(); ++p) {
        const ProjectInfo& pr = cb.projects()[p];
        s << "<details open><summary>" << h(pr.name) << " <span class=\"dim\">" << h(pr.kind == "files" || pr.kind == "folder" ? pr.version : cb.displayPath(pr.path) + " - " + pr.version) << "</span></summary><table>";
        if (!pr.configurations.empty()) { s << "<tr><th style=\"width:200px\">Configurations</th><td>"; for (size_t i = 0; i < pr.configurations.size(); ++i) { if (i) s << ", "; s << h(pr.configurations[i]); } s << "</td></tr>"; }
        if (!pr.preprocessorDefinitions.empty()) { s << "<tr><th>Preprocessor definitions</th><td class=\"mono\">"; for (size_t i = 0; i < pr.preprocessorDefinitions.size(); ++i) { if (i) s << "; "; s << h(pr.preprocessorDefinitions[i]); } s << "</td></tr>"; }
        if (!pr.includeDirs.empty()) { s << "<tr><th>Include directories</th><td class=\"mono\">"; for (size_t i = 0; i < pr.includeDirs.size(); ++i) s << h(cb.displayPath(pr.includeDirs[i])) << "<br>"; s << "</td></tr>"; }
        s << "<tr><th>Header files (" << pr.headerFiles.size() << ")</th><td class=\"mono\">";
        for (size_t i = 0; i < pr.headerFiles.size(); ++i) s << h(cb.displayPath(pr.headerFiles[i])) << (fileExists(pr.headerFiles[i]) ? "" : " <span class=\"warn\">MISSING</span>") << "<br>";
        s << "</td></tr><tr><th>Source files (" << pr.sourceFiles.size() << ")</th><td class=\"mono\">";
        for (size_t i = 0; i < pr.sourceFiles.size(); ++i) s << h(cb.displayPath(pr.sourceFiles[i])) << (fileExists(pr.sourceFiles[i]) ? "" : " <span class=\"warn\">MISSING</span>") << "<br>";
        s << "</td></tr>";
        if (!pr.otherFiles.empty()) { s << "<tr><th>Other files (" << pr.otherFiles.size() << ")</th><td class=\"mono\">"; for (size_t i = 0; i < pr.otherFiles.size(); ++i) s << h(cb.displayPath(pr.otherFiles[i])) << "<br>"; s << "</td></tr>"; }
        s << "</table></details>";
    }
    {
        std::ostringstream d; int n = 0;
        for (size_t f = 0; f < files.size(); ++f)
            if (files[f]->origin != "project" && files[f]->origin != "command line" && files[f]->origin != "folder") { d << "<li>" << h(cb.displayPath(files[f]->path)) << " <span class=\"dim\">(" << h(files[f]->origin) << ")</span></li>"; ++n; }
        s << "<p><b>Files found only through #include, not listed in the project:</b> " << n << "</p><ul class=\"mono\">" << d.str() << "</ul>";
    }
    if (!cb.messages().empty()) { s << "<p><b>Loader messages:</b></p><ul>"; for (size_t i = 0; i < cb.messages().size(); ++i) s << "<li class=\"warn\">" << h(cb.messages()[i]) << "</li>"; s << "</ul>"; }
    s << "</section>\n";

    // diagram
    s << "<section id=\"diagram\"><h2>Class diagram</h2><div class=\"diagram\">" << renderSvg(layout, cb.title(), false) << "</div></section>\n";

    // classes
    s << "<section id=\"classes\"><h2>Classes (" << (t.classes + t.structs + t.unions) << " definitions, " << t.forwardOnly << " forward declarations)</h2>";
    s << "<table><tr><th>Kind</th><th>Name</th><th>Bases</th><th>Methods</th><th>Fields</th><th>Location</th></tr>";
    for (size_t i = 0; i < classes.size(); ++i) {
        const ClassInfo& c = *classes[i];
        if (!c.isDefinition) continue;
        s << "<tr><td>" << ClassInfo::kindName(c.kind) << (c.tmpl.isTemplate ? " template" : "") << "</td><td>" << classLink(c) << "</td><td>";
        for (size_t b = 0; b < c.bases.size(); ++b) { if (b) s << ", "; s << h(c.bases[b].name); }
        s << "</td><td>" << c.methods.size() << "</td><td>" << c.fields.size() << "</td><td>" << h(locationText(cb, c.file, c.line, c.endLine)) << "</td></tr>";
    }
    s << "</table>\n";
    for (size_t i = 0; i < classes.size(); ++i) writeClass(s, cb, *classes[i], opt);
    s << "</section>\n";

    // globals
    s << "<section id=\"globals\"><h2>Global definitions</h2>";
    s << "<h3>Free functions (" << t.freeFunctions << ")</h3><table>" << kFunctionHeadNoAccess;
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t k = 0; k < files[f]->functions.size(); ++k)
            if (files[f]->functions[k].qualifier.empty()) s << functionRow(cb, files[f]->functions[k], opt, false);
    s << "</table>";
    s << "<h3>Out-of-line member definitions (" << t.memberDefinitionsOutOfLine << ")</h3><table><tr><th>Function</th><th>Defined</th><th>Declared</th></tr>";
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t k = 0; k < files[f]->functions.size(); ++k) {
            const FunctionInfo& fn = files[f]->functions[k];
            if (fn.qualifier.empty()) continue;
            s << "<tr><td><code>" << h(fn.fullName() + "(" + fn.paramText + ")" + (fn.isConst ? " const" : "")) << "</code></td><td>" << h(locationText(cb, fn.file, fn.line, fn.endLine)) << "</td><td>"
              << (fn.declaredAt.empty() ? "<span class=\"warn\">" + h(fn.linkNote.empty() ? std::string("no declaration found") : fn.linkNote) + "</span>" : h(fn.declaredAt)) << "</td></tr>";
        }
    s << "</table>";
    s << "<h3>Global variables (" << t.globals << ") and static member definitions (" << t.staticMemberDefinitions << ")</h3><table><tr><th>Declaration</th><th>Kind</th><th>Location</th><th>Notes</th></tr>";
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t v = 0; v < files[f]->variables.size(); ++v) {
            const VariableInfo& var = files[f]->variables[v];
            s << "<tr><td><code>" << h(std::string(var.isExtern ? "extern " : "") + (var.isStatic ? "static " : "") + declText(var.type, var.fullName()) + (var.initializer.empty() ? "" : " = " + var.initializer)) << "</code></td><td>"
              << (var.qualifier.empty() ? (var.isExtern ? "extern declaration" : "global") : "static member definition") << "</td><td>" << h(locationText(cb, var.file, var.line, 0)) << "</td><td>"
              << (var.definedAt.empty() ? "" : "declared " + h(var.definedAt)) << (var.condition.empty() ? "" : " #if: <code>" + h(var.condition) + "</code>") << "</td></tr>";
        }
    s << "</table>";
    s << "<h3>Typedefs, enums, namespaces, using</h3><ul class=\"plain mono\">";
    for (size_t f = 0; f < files.size(); ++f) {
        for (size_t i = 0; i < files[f]->typedefs.size(); ++i) s << "<li>" << h(files[f]->typedefs[i].text) << " <span class=\"dim\">" << h(locationText(cb, files[f]->path, files[f]->typedefs[i].line, 0)) << "</span></li>";
        for (size_t i = 0; i < files[f]->enums.size(); ++i) {
            const EnumInfo& e = files[f]->enums[i]; std::string en;
            for (size_t k = 0; k < e.enumerators.size(); ++k) { if (k) en += ", "; en += e.enumerators[k]; }
            s << "<li>enum " << h(e.scope.empty() ? e.name : e.scope + "::" + e.name) << " { " << h(en) << " } <span class=\"dim\">" << h(locationText(cb, files[f]->path, e.line, 0)) << "</span></li>";
        }
        for (size_t i = 0; i < files[f]->usings.size(); ++i) s << "<li>" << h(files[f]->usings[i].text) << " <span class=\"dim\">" << h(locationText(cb, files[f]->path, files[f]->usings[i].line, 0)) << "</span></li>";
    }
    {
        std::vector<std::string> ns;
        for (size_t f = 0; f < files.size(); ++f) for (size_t i = 0; i < files[f]->namespaces.size(); ++i) addUnique(ns, files[f]->namespaces[i]);
        for (size_t i = 0; i < ns.size(); ++i) s << "<li>namespace " << h(ns[i]) << "</li>";
    }
    s << "</ul>";
    s << "<h3>Macros (" << t.macros << ")</h3><table><tr><th>Macro</th><th>Replacement</th><th>Defined at</th></tr>";
    for (size_t f = 0; f < files.size(); ++f)
        for (size_t d = 0; d < files[f]->directives.size(); ++d) {
            const Directive& dir = files[f]->directives[d];
            if (dir.kind != Directive::Define || dir.inactive) continue;
            s << "<tr><td><code>" << h(dir.argument + dir.macroParams) << "</code></td><td><code>" << h(dir.macroBody) << "</code></td><td>" << h(locationText(cb, files[f]->path, dir.line, 0)) << (dir.isIncludeGuard ? " <span class=\"tag\">include guard</span>" : "") << "</td></tr>";
        }
    s << "</table></section>\n";

    // files
    s << "<section id=\"files\"><h2>Files (" << t.files << ")</h2>";
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        s << "<details><summary>" << (sf.isHeader ? "[H] " : "[C] ") << h(cb.displayPath(sf.path)) << " <span class=\"dim\">" << sf.lineCount << " lines, " << sf.directives.size() << " directives, "
          << sf.allClasses.size() << " classes, " << sf.functions.size() << " functions, " << sf.variables.size() << " variables; origin: " << h(sf.origin) << "</span></summary>";
        if (!sf.includeGuard.empty()) s << "<p>Include guard: <code>" << h(sf.includeGuard) << "</code></p>";
        if (sf.pragmaOnce) s << "<p><code>#pragma once</code></p>";
        if (opt.directives && !sf.directives.empty()) {
            s << "<table><tr><th style=\"width:60px\">Line</th><th>Directive</th><th>Note</th></tr>";
            for (size_t d = 0; d < sf.directives.size(); ++d) {
                const Directive& dir = sf.directives[d];
                std::string note = directiveNote(cb, dir);
                bool bad = dir.kind == Directive::Include && !dir.systemInclude && dir.resolvedPath.empty() && !dir.inactive;
                s << "<tr" << (dir.inactive ? " class=\"dim\"" : "") << "><td>L" << dir.line << "</td><td><code>#" << h(dir.text) << "</code></td><td" << (bad ? " class=\"warn\"" : "") << ">" << h(note) << "</td></tr>";
            }
            s << "</table>";
        }
        s << "<p><b>Declarations:</b></p><ul class=\"plain mono\">";
        for (size_t c = 0; c < sf.allClasses.size(); ++c) {
            const ClassInfo& cls = *sf.allClasses[c];
            s << "<li>" << ClassInfo::kindName(cls.kind) << " " << classLink(cls) << " <span class=\"dim\">L" << cls.line << (cls.isDefinition ? "-L" + intToString(cls.endLine) : " (forward)") << "</span></li>";
        }
        for (size_t k = 0; k < sf.functions.size(); ++k) {
            const FunctionInfo& fn = sf.functions[k];
            s << "<li>" << h((fn.returnType.empty() ? "" : fn.returnType + " ") + fn.fullName() + "(" + fn.paramText + ")") << " <span class=\"dim\">L" << fn.line << (fn.hasBody ? "-L" + intToString(fn.endLine) + " definition" : " declaration") << "</span></li>";
        }
        for (size_t v = 0; v < sf.variables.size(); ++v) s << "<li>" << h(declText(sf.variables[v].type, sf.variables[v].fullName())) << " <span class=\"dim\">L" << sf.variables[v].line << "</span></li>";
        s << "</ul>";
        if (!sf.diagnostics.empty()) { s << "<p><b>Parser diagnostics:</b></p><ul>"; for (size_t i = 0; i < sf.diagnostics.size(); ++i) s << "<li class=\"warn mono\">" << h(sf.diagnostics[i]) << "</li>"; s << "</ul>"; }
        s << "</details>";
    }
    s << "</section>\n";

    // tree
    s << "<section id=\"tree\"><h2>Inheritance tree</h2><pre>";
    { std::vector<std::string> lines; inheritanceTreeLines(cb, lines); if (lines.empty()) s << "(no classes)"; for (size_t i = 0; i < lines.size(); ++i) s << h(lines[i]) << "\n"; }
    s << "</pre></section>\n";

    // exceptions
    s << "<section id=\"exceptions\"><h2>Exception handling</h2><table><tr><th>Function</th><th>Location</th><th>try / catch / throw</th></tr>";
    for (size_t f = 0; f < files.size(); ++f) {
        const SourceFile& sf = *files[f];
        for (size_t k = 0; k < sf.functions.size(); ++k) {
            std::string ex = exceptionSummary(sf.functions[k].exceptions);
            if (!ex.empty()) s << "<tr><td><code>" << h(sf.functions[k].fullName() + "(" + sf.functions[k].paramText + ")") << "</code></td><td>" << h(locationText(cb, sf.path, sf.functions[k].line, 0)) << "</td><td>" << h(ex) << "</td></tr>";
        }
        for (size_t c = 0; c < sf.allClasses.size(); ++c)
            for (size_t m = 0; m < sf.allClasses[c]->methods.size(); ++m) {
                const FunctionInfo& fn = sf.allClasses[c]->methods[m];
                std::string ex = fn.hasBody ? exceptionSummary(fn.exceptions) : "";
                if (!ex.empty()) s << "<tr><td><code>" << h(sf.allClasses[c]->qualifiedName + "::" + fn.name + "(" + fn.paramText + ")") << "</code></td><td>" << h(locationText(cb, sf.path, fn.line, 0)) << "</td><td>" << h(ex) << "</td></tr>";
            }
    }
    s << "</table></section>\n";

    // health
    {
        const std::vector<Finding>& fd = cb.findings();
        int ne, nw, ni;
        countFindings(fd, ne, nw, ni);
        s << "<section id=\"health\"><h2>Health</h2><div class=\"cards\"><div class=\"card\"><b class=\"warn\">" << ne << "</b><span>errors</span></div>"
          << "<div class=\"card\"><b style=\"color:#c07a00\">" << nw << "</b><span>warnings</span></div><div class=\"card\"><b class=\"dim\">" << ni << "</b><span>info</span></div></div>";
        if (fd.empty()) s << "<p class=\"ok\">no findings</p>";
        else {
            // the columns of Visual Studio's Error List
            s << "<table><tr><th>Severity</th><th>Code</th><th>Description</th><th>Project</th><th>File</th><th>Line</th></tr>";
            for (size_t k = 0; k < fd.size(); ++k) {
                const char* cls = fd[k].severity == Finding::Error ? "warn" : (fd[k].severity == Finding::Warning ? "" : "dim");
                int project = projectNumber(cb, fd[k]);
                std::string projectName = project <= static_cast<int>(cb.projects().size()) ? cb.projects()[static_cast<size_t>(project - 1)].name : cb.title();
                s << "<tr><td class=\"" << cls << "\">" << severityName(fd[k].severity) << "</td><td>" << fd[k].code << "</td><td>" << h(fd[k].message)
                  << (fd[k].detail.empty() ? "" : "<br><span class=\"dim\">" + h(fd[k].detail) + "</span>") << "</td><td>" << project << "&gt;" << h(projectName)
                  << "</td><td>" << h(cb.displayPath(fd[k].file)) << "</td><td>" << fd[k].line << "</td></tr>";
            }
            s << "</table>";
        }
        s << "<p class=\"dim\">E = will not compile or link, W = likely bug or risky construct, I = worth knowing. Checks are static: macros are not expanded and #if conditions are not evaluated.</p></section>\n";
    }

    // totals
    s << "<section id=\"totals\"><h2>Totals</h2><pre>";
    { std::vector<std::string> lines; totalsLines(t, lines); for (size_t i = 0; i < lines.size(); ++i) s << h(lines[i]) << "\n"; }
    s << "</pre></section>\n";

    s << "</main></body></html>\n";
    return s.str();
}

} // namespace cppparse
