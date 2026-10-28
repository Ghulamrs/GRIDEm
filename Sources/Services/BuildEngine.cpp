// =============================================================================
//  BuildEngine.cpp - see BuildEngine.h
// =============================================================================
#include "BuildEngine.h"
#include "VisualStudio.h"
#include "../Core/PathUtil.h"
#include "../Core/StringUtil.h"
#include "../Platform/Environment.h"
#include "../Platform/Paths.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <sys/stat.h>

namespace GRIDEm { namespace Services {

    using Core::Language;
    using Core::LanguageInfo;
    using Core::PathUtil;
    using Core::StringUtil;
    using Core::Target;
    using Core::TargetInfo;
    using Model::BuildConfigurationPtr;
    using Model::GrideProjectPtr;
    using Platform::Paths;
    using Settings::LibraryDefinitionPtr;
    using Settings::TargetDefinitionPtr;
    using Settings::ToolchainDefinitions;
    using Settings::ToolchainDefinitionsPtr;
    using Settings::ToolchainSettings;
    using Settings::ToolchainSettingsPtr;
    using Settings::ToolDefinitionPtr;

    // ---- small helpers -------------------------------------------------------------

    // a file's time to the nanosecond (File::GetLastWriteTime); missing files read as very old
    static const long long kMissingTime = LLONG_MIN + 1;
    static const long long kMinTime = LLONG_MIN;

    static long long WriteTime(const std::string& path)
    {
        struct stat st;
        if (path.empty() || stat(path.c_str(), &st) != 0) return kMissingTime;
#if defined(__APPLE__)
        return (long long)st.st_mtimespec.tv_sec * 1000000000LL + (long long)st.st_mtimespec.tv_nsec;
#else
        return (long long)st.st_mtim.tv_sec * 1000000000LL + (long long)st.st_mtim.tv_nsec;
#endif
    }

    static bool Contains(const std::vector<std::string>& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    static void RemoveFirst(std::vector<std::string>& list, const std::string& item)
    {
        std::vector<std::string>::iterator it = std::find(list.begin(), list.end(), item);
        if (it != list.end()) list.erase(it);
    }

    static std::string FolderOf(const GrideProjectPtr& p, const std::string& src)
    {
        std::string folder;
        if (p->FolderOfFile(src, folder)) return folder;
        return std::string();
    }

    static std::string ActionName(BuildAction a)
    {
        if (a == BuildAction::Clean) return "Clean";
        if (a == BuildAction::Rebuild) return "Rebuild All";
        return "Build";
    }

    // ---- BuildSummary --------------------------------------------------------------
    BuildSummary::BuildSummary()
        : Succeeded(0), Failed(0), UpToDate(0), Skipped(0), Cancelled(false), ElapsedSeconds(0.0),
          Action(BuildAction::Build), errors(0), warnings(0) {}

    BuildMessagePtr BuildSummary::FirstError() const
    {
        std::lock_guard<std::mutex> hold(firstLock);
        return firstError;
    }

    void BuildSummary::Count(Severity s)
    {
        if (s == Severity::Error) errors++;
        else if (s == Severity::Warning) warnings++;
    }

    void BuildSummary::Count(const BuildMessagePtr& m)
    {
        Count(m->Sev);
        if (m->Sev == Severity::Error && m->Line > 0 && !m->File.empty())
        {
            std::lock_guard<std::mutex> hold(firstLock);
            if (!firstError) firstError = m;
        }
    }

    // ---- BuildEngine: public -------------------------------------------------------
    BuildEngine::BuildEngine(const Settings::GrideSettingsPtr& s)
        : settings(s), placedError(false), codeViewWarned(false), codeViewMissing(false), ownCodeView(false),
          clSettingsSaid(false), rideHeadersSaid(false), crtSaid(false), toolLines(0), toolHidden(0), busy(false)
    {
        runner.OnLine = [this](const std::string& line) { OnToolLine(line); };
    }

    BuildEngine::~BuildEngine()
    {
        Cancel();
        if (worker.joinable()) worker.join();
    }

    void BuildEngine::Cancel()
    {
        runner.Cancel();
        StopWorkers();
    }

    bool BuildEngine::StartAsync(const BuildRequestPtr& r)
    {
        std::vector<BuildRequestPtr> one;
        one.push_back(r);
        return StartBatchAsync(one);
    }

    bool BuildEngine::StartBatchAsync(const std::vector<BuildRequestPtr>& requests)
    {
        if (busy || requests.empty()) return false;
        if (worker.joinable()) worker.join();
        busy = true;
        worker = std::thread([this, requests]() { ThreadMain(requests); });
        return true;
    }

    BuildSummaryPtr BuildEngine::RunBatch(const std::vector<BuildRequestPtr>& requests)
    {
        if (busy || requests.empty()) return nullptr;
        if (worker.joinable()) worker.join();
        busy = true;
        ThreadMain(requests);
        return summary;
    }

    void BuildEngine::Wait()
    {
        if (worker.joinable()) worker.join();
    }

    std::string BuildEngine::ProgramPath(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg)
    {
        std::string name = cfg->OutputName.empty() ? p->ProgramName() : cfg->OutputName;
        return Paths::Combine(p->ToAbsolute(cfg->OutDir), name + TargetInfo::ProgramExtension(cfg->TargetKind));
    }

    bool BuildEngine::IsHeader(const std::string& path)
    {
        std::string e = StringUtil::ToLower(Paths::Extension(path));
        return e == ".h" || e == ".hpp" || e == ".hh" || e == ".hxx" || e == ".inc" || e == ".inl";
    }

    std::vector<std::string> BuildEngine::BuildableFiles(const GrideProjectPtr& p) const
    {
        return BuildableFiles(p, std::string());
    }

    std::vector<std::string> BuildEngine::BuildableFiles(const GrideProjectPtr& p, const std::string& configName) const
    {
        std::vector<std::string> r;
        NameSet seen;
        for (const std::string& f : p->AllFiles())
        {
            if (p->IsExcludedIn(f, configName) || IsHeader(f) || !p->IsBuilt(f)) continue;
            Language l = settings->DetectLanguage(f, p->Lang);
            if ((LanguageInfo::IsCompiled(l) || l == Language::Assembly) && seen.insert(f).second) r.push_back(f);
        }
        return r;
    }

    BuildEngine::StemTable BuildEngine::StemMap(const GrideProjectPtr& p, const std::vector<std::string>& sources)
    {
        std::map<std::string, int, Core::CaseInsensitiveLess> count;
        NameSet distinct;
        for (const std::string& s : sources)
        {
            if (!distinct.insert(s).second) continue;
            count[Paths::FileNameWithoutExtension(s)]++;
        }
        StemTable map;
        NameSet used;
        for (const std::string& s : sources)
        {
            std::string n = Paths::FileNameWithoutExtension(s);
            if (count[n] == 1 && map.find(s) == map.end()) { map[s] = n; used.insert(n); }
        }
        for (const std::string& s : sources)
            if (map.find(s) == map.end()) map[s] = MakeStem(p->ToRelative(s), used);
        return map;
    }

    std::string BuildEngine::MakeStem(const std::string& relPath, NameSet& used)
    {
        std::string r = PathUtil::Normalize(relPath);
        if (Paths::IsRooted(r)) r = Paths::FileName(r);
        std::string dir = Paths::DirectoryName(r);
        std::string noExt = dir.empty() ? Paths::FileNameWithoutExtension(r)
                                        : dir + "/" + Paths::FileNameWithoutExtension(r);
        std::string sb;
        for (char c : noExt) sb += (StringUtil::IsLetterOrDigit(c) || c == '-') ? c : '_';
        size_t lead = 0;
        while (lead < sb.size() && sb[lead] == '_') lead++;
        std::string stem = sb.substr(lead);
        if (stem.empty()) stem = "file";
        std::string unique = stem;
        for (int i = 2; used.find(unique) != used.end(); i++) unique = stem + "_" + std::to_string(i);
        used.insert(unique);
        return unique;
    }

    std::string BuildEngine::AsmFileFor(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg,
                                        const std::string& src) const
    {
        std::string full = Paths::FullPath(src);
        std::vector<std::string> files = BuildableFiles(p);
        if (!Contains(files, full)) files.push_back(full);
        std::string stem;
        if (settings->DetectLanguage(full, p->Lang) == Language::Shalimar && ShalimarUnit(p, files).size() > 1)
            stem = ProgramStem(p, cfg);
        else
        {
            StemTable stems = StemMap(p, files);
            StemTable::const_iterator it = stems.find(full);
            stem = it != stems.end() ? it->second : Paths::FileNameWithoutExtension(full);
        }
        return Paths::Combine(p->ToAbsolute(cfg->ObjDir), stem + settings->Toolchain(cfg->TargetKind)->AsmExt);
    }

    std::vector<std::string> BuildEngine::ShalimarUnit(const GrideProjectPtr& p, const std::vector<std::string>& sources) const
    {
        std::vector<std::string> r;
        for (const std::string& s : sources)
            if (settings->DetectLanguage(s, p->Lang) == Language::Shalimar) r.push_back(s);
        return r;
    }

    std::string BuildEngine::ProgramStem(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg)
    {
        return cfg->OutputName.empty() ? p->ProgramName() : cfg->OutputName;
    }

    std::string BuildEngine::Unwritable(const std::string& dir)
    {
        PathUtil::EnsureDirectory(dir);
        std::string probe = Paths::Combine(dir, ".gride-writes-here");
        std::string error;
        if (Paths::IsDirectory(dir) && Paths::WriteFile(probe, std::string(), error) && Paths::DeleteFile(probe))
            return std::string();
        return dir + " cannot be written to, and the program would go there - copy the project to a folder of your own, such as Documents, and build it there";
    }

    std::string BuildEngine::Shown(const std::string& line, const std::string& projectDir)
    {
        if (line.empty() || projectDir.empty()) return line;
        std::string dir = projectDir;
        while (!dir.empty() && (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')) dir.erase(dir.size() - 1);
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) lead++;
        std::string t = line.substr(lead);
        const std::string prefixes[] = { dir + "/", StringUtil::ReplaceChar(dir, '/', '\\') + "\\" };
        for (const std::string& d : prefixes)
            if (t.size() > d.size() && StringUtil::StartsWithIgnoreCase(t, d)) return line.substr(0, lead) + t.substr(d.size());
        return line;
    }

    // ---- output --------------------------------------------------------------------
    void BuildEngine::Say(const std::string& s)
    {
        if (OnOutput) OnOutput(s);
        Keep(s);
    }

    void BuildEngine::Keep(const std::string& s)
    {
        std::lock_guard<std::mutex> hold(linesLock);
        projectLines.push_back(s);
    }

    bool BuildEngine::OutputHas(const std::string& part)
    {
        std::lock_guard<std::mutex> hold(linesLock);
        for (const std::string& l : projectLines)
            if (StringUtil::ContainsIgnoreCase(l, part)) return true;
        return false;
    }

    void BuildEngine::BeginTool(const std::string& source, const std::string& toolName)
    {
        reader = std::make_shared<DiagnosticReader>(currentWorkDir, currentPattern, source, toolName);
        toolLines = 0;
        toolHidden = 0;
    }

    // RIDE's own compilers write UTF-8 unless the toolchain names a code page
    ToolRunOptions BuildEngine::ToolOptions(const ToolRunOptions& options, bool rideTool)
    {
        ToolRunOptions o = options;
        if (rideTool && o.OutputCodePage == 0) o.OutputCodePage = 65001;
        return o;
    }

    int BuildEngine::RunTool(const std::string& command, const std::string& workDir, const ToolRunOptions& options,
                             const std::string& source, const std::string& toolName, const std::string& targetName,
                             bool rideTool)
    {
        BeginTool(source, toolName);
        ToolRunOptions o = ToolOptions(options, rideTool);
        int rc = runner.Run(command, workDir, &o);
        EndTool(rc, source, toolName, targetName);
        return rc;
    }

    // after the run: the lines not shown counted, then the tool's lines about itself - errors only when it failed
    void BuildEngine::EndTool(int rc, const std::string& source, const std::string& toolName, const std::string& targetName)
    {
        if (toolHidden > 0) Say("[" + toolName + ": " + std::to_string(toolHidden) + " more line(s) not shown]");
        std::shared_ptr<DiagnosticReader> r = reader;
        if (r)
            for (const BuildMessagePtr& m : r->TakeItself(rc != 0 && rc != ToolRunner::ExitCancelled))
            {
                if (currentProject) m->Project = currentProject->Name;
                summary->Count(m);
                if (OnMessage) OnMessage(m);
            }
        if (rc == ToolRunner::ExitStartFailed)
            Report(Severity::Error, source, toolName + " could not be run - name it under Settings > Toolchains > " + targetName + ", or put it on PATH");
    }

    void BuildEngine::OnToolLine(const std::string& line)
    {
        Keep(line);
        std::shared_ptr<DiagnosticReader> r = reader;
        BuildMessagePtr m = r ? r->Feed(line) : BuildMessageParser::TryParse(line, currentWorkDir, currentPattern);
        // a tool failing on every line would bury the build: its first MaxToolLines are shown, the rest counted
        if (++toolLines > MaxToolLines)
        {
            toolHidden++;
            if (m) summary->Count(m);
            return;
        }
        // the compilers' banner and their echo of the file written say nothing the step's line has not
        std::string t = StringUtil::Trim(line);
        bool quiet = !m && (StringUtil::StartsWith(t, "\xC2\xA9") || (!writes.empty() && StringUtil::EqualsIgnoreCase(t, writes)));
        if (!quiet && OnOutput) OnOutput(Shown(line, currentWorkDir));
        if (!m) return;
        if (currentProject) m->Project = currentProject->Name;
        if (m->Sev == Severity::Error && m->Line > 0 && !m->File.empty()) placedError = true;
        summary->Count(m);
        if (OnMessage) OnMessage(m);
    }

    void BuildEngine::Report(Severity sev, const std::string& file, const std::string& text)
    {
        BuildMessagePtr m = std::make_shared<BuildMessage>();
        m->Sev = sev;
        m->File = file;
        m->Text = text;
        m->Project = currentProject ? currentProject->Name : std::string();
        summary->Count(sev);
        std::string prefix = "info";
        if (sev == Severity::Error) prefix = "error";
        else if (sev == Severity::Warning) prefix = "warning";
        Say(prefix + ": " + text);
        if (OnMessage) OnMessage(m);
    }

    // ---- compile steps on workers ------------------------------------------------------
    // how many compile steps run at once (Settings > General): 0 is one per processor
    int BuildEngine::Jobs() const
    {
        int n = settings->Env->BuildJobs;
        if (n > 0) return n;
        unsigned int cpus = std::thread::hardware_concurrency();
        return cpus > 0 ? (int)cpus : 1;
    }

    void BuildEngine::StopWorkers()
    {
        std::lock_guard<std::mutex> hold(workersLock);
        for (ToolRunner* r : workers) r->Cancel();
    }

    void BuildEngine::RunStep(const CompileStepPtr& step, const std::string& workDir)
    {
        ToolRunner r;
        std::vector<std::string>* into = nullptr;
        r.OnLine = [&into](const std::string& line) { if (into) into->push_back(line); };
        {
            std::lock_guard<std::mutex> hold(workersLock);
            workers.push_back(&r);
            if (runner.Cancelled()) r.Cancel();
        }
        for (ToolPass& pass : step->Passes)
        {
            into = &pass.Lines;
            pass.Code = r.Run(pass.Command, workDir, &pass.Options);
            pass.Ran = true;
            if (pass.Code != 0)
            {
                if (!pass.Writes.empty() && Paths::IsFile(pass.Writes)) Paths::DeleteFile(pass.Writes);
                break;
            }
        }
        into = nullptr;
        {
            std::lock_guard<std::mutex> hold(workersLock);
            workers.erase(std::remove(workers.begin(), workers.end(), &r), workers.end());
        }
        {
            std::lock_guard<std::mutex> hold(stepLock);
            step->Done = true;
        }
        stepDone.notify_all();
    }

    // The steps Jobs() at a time, the largest first, each read back in project order once it and those
    // before it are done - so the output is what one tool after another gave. The first to fail stops
    // the others. -1 when one failed or the build was cancelled, else 0.
    int BuildEngine::RunSteps(const std::vector<CompileStepPtr>& steps, const std::string& workDir)
    {
        int jobs = Jobs();
        std::vector<CompileStepPtr> order = steps;
        std::stable_sort(order.begin(), order.end(),
                         [](const CompileStepPtr& a, const CompileStepPtr& b) { return a->Size > b->Size; });
        std::vector<std::thread> threads;
        size_t started = 0, read = 0;
        bool failed = false;
        int result = 0;
        for (;;)
        {
            while (!failed && read < steps.size() && steps[read]->Done)
                if (ReadBack(*steps[read++]) != 0) { failed = true; StopWorkers(); }
            int running = 0;
            for (const CompileStepPtr& s : steps)
                if (s->Started && !s->Done) running++;
            if (failed || runner.Cancelled())
            {
                if (running == 0) { result = -1; break; }
            }
            else
            {
                if (read == steps.size()) break;
                while (running < jobs && started < order.size())
                {
                    CompileStepPtr next = order[started++];
                    next->Started = true;
                    threads.push_back(std::thread([this, next, workDir]() { RunStep(next, workDir); }));
                    running++;
                }
            }
            std::unique_lock<std::mutex> wait(stepLock);
            stepDone.wait_for(wait, std::chrono::milliseconds(100));
        }
        for (std::thread& t : threads) t.join();
        return result;
    }

    // a finished step read as RunTool reads a tool while it runs; 0, or -1 with failedStep where it failed
    int BuildEngine::ReadBack(CompileStep& step)
    {
        for (ToolPass& pass : step.Passes)
        {
            for (const std::string& s : pass.Before) Say(s);
            if (!pass.Ran) return -1;
            BeginTool(pass.Source, pass.ToolName);
            writes = pass.Echo;
            for (const std::string& line : pass.Lines) OnToolLine(line);
            writes.clear();
            EndTool(pass.Code, pass.Source, pass.ToolName, pass.TargetName);
            if (pass.Code == 0) continue;
            failedStep = pass.Step;
            if (!pass.Refused.empty()) Say(pass.Refused);
            return -1;
        }
        return 0;
    }

    // whether a Debug build has a file whose compiler cannot write its own CodeView, which clang must then carry
    bool BuildEngine::NeedsClang(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg, Target t) const
    {
        for (const std::string& src : BuildableFiles(p, cfg->Config))
        {
            Language lang = settings->DetectLanguage(src, p->Lang);
            if (lang == Language::Assembly) continue;
            RideTool tool = RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(FolderOf(p, src))), lang);
            if (RideToolchain::DebugsWithCodeView(tool, t, true) && !RideToolchain::OwnCodeView(tool, t, true)) return true;
        }
        return false;
    }

    // The Windows C runtime a link takes: "grt" (GRIDE's) or "msvc" (Microsoft's). Automatic is Microsoft's
    // only where Visual Studio is found and cl's objects are among the inputs.
    std::string BuildEngine::WindowsRuntime(bool clObjects) const
    {
        std::string choice = settings->Env->EffectiveWindowsRuntime();
        if (choice != "auto") return choice;
        return clObjects && VisualStudio::Find() ? "msvc" : "grt";
    }

    // Microsoft's runtime libraries, each refused to a link with GRIDE's runtime
    static const char* const kMicrosoftRuntimeRefused =
        " /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd /NODEFAULTLIB:msvcrt /NODEFAULTLIB:msvcrtd"
        " /NODEFAULTLIB:libucrt /NODEFAULTLIB:libucrtd /NODEFAULTLIB:ucrt /NODEFAULTLIB:ucrtd"
        " /NODEFAULTLIB:libvcruntime /NODEFAULTLIB:libvcruntimed /NODEFAULTLIB:vcruntime /NODEFAULTLIB:vcruntimed"
        " /NODEFAULTLIB:libcpmt /NODEFAULTLIB:libcpmtd /NODEFAULTLIB:msvcprt /NODEFAULTLIB:msvcprtd"
        " /NODEFAULTLIB:oldnames /NODEFAULTLIB:legacy_stdio_definitions";

    void BuildEngine::ThreadMain(std::vector<BuildRequestPtr> requests)
    {
        BuildRequestPtr first = requests[0];
        summary = std::make_shared<BuildSummary>();
        summary->Action = first->Action;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        runner.Reset();
        try
        {
            for (const BuildRequestPtr& r : requests)
            {
                for (const GrideProjectPtr& p : r->Projects)
                {
                    if (runner.Cancelled()) break;
                    currentProject = p;
                    {
                        std::lock_guard<std::mutex> hold(linesLock);
                        projectLines.clear();
                    }
                    failedStep.clear();
                    placedError = false;
                    codeViewWarned = false;
                    clSettingsSaid = false;
                    rideHeadersSaid = false;
                    crtSaid = false;
                    int result = BuildProject(p, r, false);
                    if (result == -1 && !runner.Cancelled()) result = NativeFallback(p, r);
                    if (result > 0) summary->Succeeded++;
                    else if (result == 0) summary->UpToDate++;
                    else if (result == -2) summary->Skipped++;
                    else summary->Failed++;
                }
            }
        }
        catch (const std::exception& ex)
        {
            Report(Severity::Error, std::string(), std::string("internal build error: ") + ex.what());
            summary->Failed++;
        }
        summary->Cancelled = runner.Cancelled();
        summary->ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        currentProject = nullptr;

        std::string verb = ActionName(first->Action);
        if (summary->Cancelled) Say("========== " + verb + " cancelled ==========");
        else Say("========== " + verb + ": " + std::to_string(summary->Succeeded) + " succeeded, " +
                 std::to_string(summary->Failed) + " failed, " + std::to_string(summary->UpToDate) + " up-to-date, " +
                 std::to_string(summary->Skipped) + " skipped ==========");
        char elapsed[64];
        std::snprintf(elapsed, sizeof elapsed, "Elapsed %.2f s", summary->ElapsedSeconds);
        Say(elapsed);
        busy = false;
        if (OnFinished) OnFinished(summary);
    }

    // ---- the native-tools question (RIDE's nativeFallbackWanted) ---------------------
    // A failed build the vendor's tools might make: one of the project's own assembler/linker
    // failed, the compilers found no fault in the source, nothing in the output names an
    // unresolved or duplicate symbol, and the settings say to ask. Yes builds again with the
    // target's nativeAssembler / nativeLinker; no, or nobody to ask, and the build stands.
    int BuildEngine::NativeFallback(const GrideProjectPtr& p, const BuildRequestPtr& r)
    {
        if (!settings->Env->AskNativeTools) return -1;
        if (failedStep != "assemble" && failedStep != "link") return -1;
        if (placedError) return -1;
        static const char* const faults[] = { "unresolved external", "undefined symbol", "no symbol index", "LNK2001",
                                              "LNK2019", "LNK1120", "LNK2005", "already defined", "multiply defined",
                                              "redefined" };
        for (const char* fault : faults)
            if (OutputHas(fault)) return -1;
        Target t = r->TargetKind;
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        ToolchainSettingsPtr tc = settings->Toolchain(t);
        bool emulated = RideToolchain::IsEmulated(t);
        ToolDefinitionPtr nas = emulated ? nullptr : defs->ToolFor(t, "nativeAssembler");
        ToolDefinitionPtr nld = defs->ToolFor(t, "nativeLinker");
        bool as = nas && !StringUtil::IsNullOrWhiteSpace(tc->Assembler);
        bool ld = nld && !StringUtil::IsNullOrWhiteSpace(tc->Linker);
        if (!as && !ld) return -1;
        std::string ours = as && ld ? RoleName(t, "assembler", tc->Assembler) + " and " + RoleName(t, "linker", tc->Linker)
                         : as ? RoleName(t, "assembler", tc->Assembler) : RoleName(t, "linker", tc->Linker);
        std::string theirs;
        if (emulated) theirs = nld->Name;
        else if (as && ld) theirs = "Visual Studio's " + nas->Name + " and " + nld->Name;
        else if (as) theirs = "Visual Studio's " + nas->Name;
        else theirs = "Microsoft's " + nld->Name;
        std::string nasPath = as ? tc->ResolveFor("nativeAssembler", nas->Exe) : std::string();
        std::string nldPath = ld ? tc->ResolveFor("nativeLinker", nld->Exe) : std::string();
        bool available = (!as || Paths::IsFile(nasPath)) && (!ld || Paths::IsFile(nldPath));
        if (!available)
        {
            Say("The project's own " + ours + " did not build it, and " + theirs +
                (emulated ? " is not on this machine - Settings names TI's C6000 compiler directory"
                          : " are not on this machine - no Visual Studio was found"));
            return -1;
        }
        std::string question = "The project's own " + ours + " did not build it. Use " + theirs + " for this build instead?";
        if (!Asker)
        {
            Say(question + " (nothing here can ask - the build stands)");
            return -1;
        }
        if (!Asker(question)) return -1;
        Say("building again with " + theirs + " in place of the project's " + ours + ", as asked");
        failedStep.clear();
        int again = BuildProject(p, r, true);
        Say(again > 0 ? "[built with " + theirs + ", not GRIDE's " + ours + "]"
                      : "[not built: " + theirs + " failed as well]");
        return again;
    }

    // the printed name of a tool the settings name (masm for masm.exe)
    std::string BuildEngine::ToolName(const std::string& tool)
    {
        ToolDefinitionPtr d = ToolchainDefinitions::Current()->ToolByName(tool);
        return d ? d->Name : Paths::FileNameWithoutExtension(PathUtil::Normalize(tool));
    }

    // the printed name of the tool a role names, when the setting still names it
    std::string BuildEngine::RoleName(Target t, const std::string& role, const std::string& tool)
    {
        ToolDefinitionPtr d = ToolchainDefinitions::Current()->ToolFor(t, role);
        if (d && d->HasName(tool)) return d->Name;
        return ToolName(tool);
    }

    // the definition of the program a compile runs
    ToolDefinitionPtr BuildEngine::ProgramDefinition(RideTool tool, const std::string& program)
    {
        ToolDefinitionPtr own = RideToolchain::Definition(tool);
        if (own && own->HasName(program)) return own;
        return program.empty() ? nullptr : ToolchainDefinitions::Current()->ToolByName(program);
    }

    // which compilers the build's sources go to: cl, or anything else
    void BuildEngine::PartsOf(const GrideProjectPtr& p, const std::vector<std::string>& sources, bool& cl, bool& other) const
    {
        cl = false;
        other = false;
        for (const std::string& src : sources)
        {
            Language lang = settings->DetectLanguage(src, p->Lang);
            if (lang != Language::Assembly &&
                RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(FolderOf(p, src))), lang) == RideTool::Cl)
                cl = true;
            else other = true;
        }
    }

    bool BuildEngine::NeedsCxxRuntime(const GrideProjectPtr& p, const std::vector<std::string>& sources) const
    {
        for (const std::string& src : sources)
        {
            Language lang = settings->DetectLanguage(src, p->Lang);
            if (lang == Language::Assembly) continue;
            RideTool tool = RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(FolderOf(p, src))), lang);
            if (tool == RideTool::Cpp11 || tool == RideTool::Shalimar || tool == RideTool::HostCxx) return true;
        }
        return false;
    }

    // a Visual Studio project's own cl settings as the switches the tool's projectSettings name
    std::string BuildEngine::ProjectToolFlags(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg,
                                              const ToolDefinitionPtr& d, std::vector<std::string>* unmapped)
    {
        if (!p || !p->IsVs() || !p->Vs || !p->Vs->Reading || !d) return std::string();
        Model::NoCaseMap::const_iterator mapped = p->Vs->MappedConfigs.find(cfg->Config);
        if (mapped == p->Vs->MappedConfigs.end()) return std::string();
        Model::VsProjectConfigPtr c = p->Vs->Reading->Config(mapped->second);
        if (!c) return std::string();
        std::vector<std::string> flags;
        for (const std::string& setting : d->ProjectSettingOrder)
        {
            Model::NoCaseMap::const_iterator v = c->ClSettings.find(setting);
            if (v == c->ClSettings.end()) continue;
            std::string f;
            if (d->ProjectSettingFlag(setting, v->second, f) && !StringUtil::IsNullOrWhiteSpace(f))
                flags.push_back(StringUtil::Trim(f));
        }
        if (unmapped != nullptr)
            for (const std::string& name : c->ClSettingOrder)
            {
                Model::NoCaseMap::const_iterator v = c->ClSettings.find(name);
                if (v == c->ClSettings.end()) continue;
                std::string f;
                if (!d->ProjectSettingFlag(v->first, v->second, f)) unmapped->push_back(v->first + " " + v->second);
            }
        if (!c->AdditionalOptions.empty()) flags.push_back(c->AdditionalOptions);
        return StringUtil::Join(" ", flags);
    }

    // ---- one step's tool ---------------------------------------------------------------
    BuildEngine::Step BuildEngine::AssemblerFor(const ToolchainSettingsPtr& tc, Target t, bool debug, bool native, RideTool tool)
    {
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        Step s;
        if (native && !RideToolchain::IsEmulated(t))
        {
            ToolDefinitionPtr d = defs->ToolFor(t, "nativeAssembler");
            if (d)
            {
                s.Path = tc->ResolveFor("nativeAssembler", d->Exe); s.Name = d->Name; s.Macro = "NativeAssembler"; s.Role = "nativeAssembler";
                s.Cmd = "\"$(NativeAssembler)\" " + d->OperationArgs("assemble");
                return s;
            }
        }
        if (RideToolchain::DebugsWithCodeView(tool, t, debug) && !StringUtil::IsNullOrWhiteSpace(tc->DebugAssembler)
            && !StringUtil::IsNullOrWhiteSpace(tc->DebugAssembleCmd))
        {
            std::string path = tc->ResolveFor("debugAssembler", tc->DebugAssembler);
            if (Paths::IsFile(path))
            {
                s.Path = path; s.Name = RoleName(t, "debugAssembler", tc->DebugAssembler); s.Macro = "DebugAssembler";
                s.Role = "debugAssembler"; s.Cmd = tc->DebugAssembleCmd;
                return s;
            }
            if (!codeViewWarned)
            {
                codeViewWarned = true;
                Report(Severity::Warning, std::string(), RoleName(t, "debugAssembler", tc->DebugAssembler) + " not found: " + path +
                       " - a Debug build's CodeView goes through it; assembled with " + RoleName(t, "assembler", tc->Assembler) +
                       " instead, so the debugger will see no lines (looked in " + tc->LookedIn(tc->DebugAssembler) + ")");
            }
        }
        s.Path = tc->ResolveFor("assembler", tc->Assembler); s.Name = RoleName(t, "assembler", tc->Assembler);
        s.Macro = "Assembler"; s.Role = "assembler"; s.Cmd = tc->AssembleCmd;
        return s;
    }

    BuildEngine::Step BuildEngine::LinkerFor(const ToolchainSettingsPtr& tc, Target t, bool debug, bool native,
                                             bool codeView, bool microsoft)
    {
        (void)debug;
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        Step s;
        if (native)
        {
            ToolDefinitionPtr d = defs->ToolFor(t, "nativeLinker");
            if (d)
            {
                s.Path = tc->ResolveFor("nativeLinker", d->Exe); s.Name = d->Name; s.Macro = "NativeLinker"; s.Role = "nativeLinker";
                s.Cmd = "\"$(NativeLinker)\" " + d->OperationArgs("link");
                return s;
            }
        }
        TargetDefinitionPtr td = defs->TargetOf(t);
        if (microsoft && td && td->Host)
        {
            if (!StringUtil::IsNullOrWhiteSpace(tc->DebugLinker) && !StringUtil::IsNullOrWhiteSpace(tc->DebugLinkCmd))
            {
                std::string path = tc->ResolveFor("debugLinker", tc->DebugLinker);
                if (Paths::IsFile(path))
                {
                    s.Path = path; s.Name = RoleName(t, "debugLinker", tc->DebugLinker); s.Macro = "DebugLinker";
                    s.Role = "debugLinker"; s.Cmd = tc->DebugLinkCmd;
                    return s;
                }
            }
            ToolDefinitionPtr d = defs->ToolFor(t, "nativeLinker");
            std::string theirs = d ? tc->ResolveFor("nativeLinker", d->Exe) : std::string();
            if (Paths::IsFile(theirs))
            {
                s.Path = theirs; s.Name = d->Name; s.Macro = "NativeLinker"; s.Role = "nativeLinker";
                s.Cmd = "\"$(NativeLinker)\" " + d->OperationArgs("link");
                return s;
            }
            Report(Severity::Warning, std::string(), "Microsoft's link.exe not found - cl's objects are linked with it; linked with " +
                   RoleName(t, "linker", tc->Linker) + " instead (looked in " +
                   tc->LookedIn(StringUtil::IsNullOrWhiteSpace(tc->DebugLinker) ? (d ? d->Exe : std::string()) : tc->DebugLinker) + ")");
        }
        if (codeView && !StringUtil::IsNullOrWhiteSpace(tc->DebugLinker) && !StringUtil::IsNullOrWhiteSpace(tc->DebugLinkCmd))
        {
            std::string path = tc->ResolveFor("debugLinker", tc->DebugLinker);
            if (Paths::IsFile(path))
            {
                s.Path = path; s.Name = RoleName(t, "debugLinker", tc->DebugLinker); s.Macro = "DebugLinker";
                s.Role = "debugLinker"; s.Cmd = tc->DebugLinkCmd;
                return s;
            }
            Report(Severity::Warning, std::string(), "Microsoft's " + RoleName(t, "debugLinker", tc->DebugLinker) + " not found: " + path +
                   " - a Debug build's PDB needs it; linked with " + RoleName(t, "linker", tc->Linker) +
                   " instead, so the debugger will see no lines (looked in " + tc->LookedIn(tc->DebugLinker) + ")");
        }
        s.Path = tc->ResolveFor("linker", tc->Linker); s.Name = RoleName(t, "linker", tc->Linker);
        s.Macro = "Linker"; s.Role = "linker"; s.Cmd = tc->LinkCmd;
        return s;
    }

    // ---- per project ---------------------------------------------------------------
    // returns 1 = built, 0 = up to date, -1 = failed, -2 = skipped
    int BuildEngine::BuildProject(const GrideProjectPtr& p, const BuildRequestPtr& r, bool native)
    {
        Target t = r->TargetKind;
        Say("------ " + ActionName(r->Action) + " started: Project " + p->Name + ", Configuration " + r->Config + " | " +
            TargetInfo::Name(t) + " ------");

        if (!p->SupportsTarget(t))
        {
            Say("Project " + p->Name + " does not list target " + TargetInfo::Name(t) + " - skipped.");
            return -2;
        }

        ToolchainSettingsPtr tc = settings->Toolchain(t);
        BuildConfigurationPtr cfg = p->GetConfiguration(r->Config, t);
        std::string objDir = p->ToAbsolute(cfg->ObjDir);
        std::string outDir = p->ToAbsolute(cfg->OutDir);
        bool debug = cfg->IsDebug();
        bool emulated = RideToolchain::IsEmulated(t);
        currentWorkDir = p->ProjectDir();
        currentPattern = BuildMessageParser::Compile(tc->ErrorPattern);
        if (!currentPattern && !StringUtil::IsNullOrWhiteSpace(tc->ErrorPattern))
            Report(Severity::Warning, std::string(), "the error pattern of toolchain " + tc->Name() +
                   " is not a valid regular expression and is ignored");
        ToolRunOptions run = RunOptions(tc, t);
        bool viaSetup = false;      // a setup script is Windows only: tools never come from its PATH here

        // a Debug build's line table goes through clang; without it the compilers write MASM and masm and the
        // linker make the program - said once, before anything runs. Only where the target is this machine's.
        codeViewMissing = false;
        ownCodeView = false;
        TargetDefinitionPtr hostDef = ToolchainDefinitions::Current()->TargetOf(t);
        if (debug && !native && !emulated && hostDef && hostDef->Host && r->Action != BuildAction::Clean &&
            RideToolchain::DebugsWithCodeView(RideTool::C90, t, true) && NeedsClang(p, cfg, t))
        {
            std::string clang = StringUtil::IsNullOrWhiteSpace(tc->DebugAssembler) ? std::string()
                                                                                    : tc->ResolveFor("debugAssembler", tc->DebugAssembler);
            if (clang.empty() || !Paths::IsFile(clang) || StringUtil::IsNullOrWhiteSpace(tc->DebugAssembleCmd))
            {
                codeViewMissing = true;
                codeViewWarned = true;
                Report(Severity::Warning, std::string(), RoleName(t, "debugAssembler", tc->DebugAssembler) +
                       " not found - this Debug build has no source line table: " + RoleName(t, "assembler", tc->Assembler) +
                       " and " + RoleName(t, "linker", tc->Linker) + " make the program, the debugger shows no lines (looked in " +
                       tc->LookedIn(tc->DebugAssembler) + ")");
            }
        }

        if (r->Action == BuildAction::Clean || r->Action == BuildAction::Rebuild)
        {
            // OutDir "." or "../" would otherwise empty the project (or a parent) folder
            if (!CanClean(p, objDir, "object") || !CanClean(p, outDir, "output")) return -1;
            CleanDir(objDir);
            if (!PathUtil::SamePath(objDir, outDir)) CleanDir(outDir);
            if (r->Action == BuildAction::Clean) return 1;
        }

        // RIDE refuses a build whose program could not be written where it would go
        const std::string dirs[] = { outDir, objDir };
        for (const std::string& dir : dirs)
        {
            std::string why = Unwritable(dir);
            if (!why.empty()) { Report(Severity::Error, std::string(), why); return -1; }
        }

        MacroExpander mx = BaseMacros(p, cfg, tc, r);
        std::vector<std::string> objects;
        bool anyWork = false;
        long long newestHeader = NewestHeader(p);

        std::vector<std::string> sources = BuildableFiles(p, cfg->Config);
        std::vector<std::string> named = sources;      // the file set object names are chosen from
        if (r->Action == BuildAction::CompileFile)
        {
            sources.clear();
            if (!r->SingleFile.empty())
            {
                std::string single = Paths::FullPath(r->SingleFile);
                sources.push_back(single);
                if (!Contains(named, single)) named.push_back(single);
            }
        }
        StemTable stems = StemMap(p, named);
        // a Shalimar program is one compilation of every .shl (Compile File takes the one file alone)
        std::vector<std::string> unit = r->Action == BuildAction::CompileFile ? std::vector<std::string>() : ShalimarUnit(p, sources);
        bool unitDone = false;
        bool shalimarProgram = false;
        bool crossTarget = false;
        RideTool crossTool = RideTool::C90;
        bool codeView = false;
        std::string lastFolder;

        // ---- compile + assemble each source: every step decided here, in project order, before any tool
        // runs; then RunSteps runs them Jobs() at a time ----
        if (r->Action != BuildAction::LinkOnly)
        {
            std::vector<CompileStepPtr> steps;
            for (const std::string& src : sources)
            {
                if (runner.Cancelled()) return -1;
                Language lang = settings->DetectLanguage(src, p->Lang);
                std::string name = stems[src];
                const std::vector<std::string>* group = nullptr;
                if (lang == Language::Shalimar && unit.size() > 1)
                {
                    if (unitDone) continue;
                    unitDone = true;
                    group = &unit;
                    name = ProgramStem(p, cfg);
                }
                std::string asmFile = lang == Language::Assembly ? src : Paths::Combine(objDir, name + tc->AsmExt);
                std::string objFile = Paths::Combine(objDir, name + tc->ObjExt);
                objects.push_back(objFile);

                // which compiler takes the file: the folder's word, else the project's, else the language
                std::string folder = FolderOf(p, src);
                RideTool tool = RideToolchain::Resolve(RideToolchain::FromWord(p->ToolchainFor(folder)), lang);
                if (lang != Language::Assembly && !RideToolchain::CanCompile(tool, lang))
                {
                    Report(Severity::Error, src, RideToolchain::Refusal(tool, lang));
                    return -1;
                }
                if (lang == Language::Shalimar) shalimarProgram = true;
                // a cross host target only reaches assembly on this machine (whyNotRun)
                bool crossOnly = lang != Language::Assembly && !RideToolchain::RunsHere(tool, t);
                if (crossOnly) { crossTarget = true; crossTool = tool; }

                long long newestSource = WriteTime(src);
                if (group != nullptr)
                    for (const std::string& g : *group)
                        if (WriteTime(g) > newestSource) newestSource = WriteTime(g);
                std::string made = crossOnly ? asmFile : objFile;
                bool upToDate = r->Action == BuildAction::Build && Paths::IsFile(made) &&
                                WriteTime(made) >= newestSource && WriteTime(made) >= newestHeader;
                if (upToDate) continue;
                anyWork = true;

                mx.Set("Source", src);
                mx.Set("Sources", group != nullptr ? MacroExpander::JoinQuoted(*group)
                                                   : MacroExpander::JoinQuoted(std::vector<std::string>(1, src)));
                mx.Set("SourceDir", Paths::DirectoryName(src));
                mx.Set("Name", name);
                mx.Set("AsmFile", asmFile);
                mx.Set("ObjFile", objFile);
                SetCompilerMacros(mx, tool, lang, p, cfg, tc);
                CompileStepPtr step = std::make_shared<CompileStep>();
                const std::vector<std::string> one(1, src);
                for (const std::string& f : group != nullptr ? *group : one)
                {
                    long long size = Paths::FileSize(f);
                    if (size > 0) step->Size += size;
                }
                steps.push_back(step);

                if (lang != Language::Assembly && r->Action != BuildAction::AssembleOnly)
                {
                    ToolPass compile;
                    // RIDE's "$ group (compiler)" line as each part starts
                    std::string heading = (folder.empty() ? p->Name : folder) + " (" + RideToolchain::Name(tool) + ")";
                    if (heading != lastFolder) { compile.Before.push_back("$ " + heading); lastFolder = heading; }

                    std::string compiler = tc->ResolveFor(RideToolchain::RoleOf(tool), tc->CompilerFor(tool));
                    if (!Paths::IsFile(compiler) && !viaSetup)
                    {
                        for (const std::string& s : compile.Before) Say(s);
                        Report(Severity::Error, src, RideToolchain::Name(tool) + " not found: " + compiler +
                               "  (looked in " + tc->LookedIn(tc->CompilerFor(tool)) + "; set it in Settings > Toolchains > " + tc->Name() + ")");
                        return -1;
                    }
                    mx.Set("Compiler", compiler);
                    std::string templ = tc->CompileCmd;
                    if (tool == RideTool::Cl || tool == RideTool::HostCxx)
                    {
                        if (!StringUtil::IsNullOrWhiteSpace(tc->CompileCmdCl)) templ = tc->CompileCmdCl;
                    }
                    else if (tool == RideTool::Shalimar && !StringUtil::IsNullOrWhiteSpace(tc->CompileCmdShalimar))
                        templ = tc->CompileCmdShalimar;
                    // the line names the program that runs and the file it writes: a template naming $(ObjFile)
                    // has the compiler write the object itself, else the assembly
                    bool writesObject = StringUtil::Contains(templ, "$(ObjFile)");
                    ToolDefinitionPtr ran = ProgramDefinition(tool, compiler);
                    std::string ranName = ran ? ran->Name : Paths::FileNameWithoutExtension(compiler);
                    std::string shownSources = group != nullptr ? StringUtil::Join(" ", Leaves(*group)) : Paths::FileName(src);
                    compile.Before.push_back("[" + ranName + "]  " + shownSources + "  ->  " +
                                             PathUtil::MakeRelative(p->ProjectDir(), writesObject ? objFile : asmFile));
                    bool own = !writesObject && RideToolchain::OwnCodeView(tool, t, debug);
                    bool cv = !writesObject && !own && !codeViewMissing && RideToolchain::DebugsWithCodeView(tool, t, debug);
                    if (own) ownCodeView = true;
                    std::vector<std::string> optionEnv = RideToolchain::OptionEnvironment(p->Options, RideToolchain::Definition(tool), cfg->Config);
                    // RIDE's own compilers write UTF-8; a program the definitions know is read as they say
                    bool utf8 = ran ? StringUtil::EqualsIgnoreCase(ran->OutputEncoding, "utf-8")
                                    : tool != RideTool::Cl && tool != RideTool::HostCxx;
                    compile.Command = mx.Expand(templ);
                    compile.Options = ToolOptions(CompileOptions(run, tc, cv, optionEnv), utf8);
                    compile.Source = src;
                    compile.ToolName = ranName;
                    compile.TargetName = tc->Name();
                    compile.Echo = writesObject ? objFile : asmFile;
                    compile.Writes = compile.Echo;
                    compile.Step = "compile";
                    step->Passes.push_back(compile);
                    if (writesObject) continue;      // the compiler writes the object itself: nothing to assemble
                    if (cv) codeView = true;
                }

                if (crossOnly) continue;
                if (emulated && p->EmulateOnly) continue;     // the assembly is the program: vm6747 runs it
                if (tc->AssembleCmd.empty()) continue;
                Step as = AssemblerFor(tc, t, debug, native, tool);
                if (!Paths::IsFile(as.Path) && !viaSetup)
                {
                    Report(Severity::Error, src, as.Name + " not found: " + as.Path +
                           "  (looked in " + tc->LookedIn(as.Role == "assembler" ? tc->Assembler : as.Path) +
                           "; set it in Settings > Toolchains > " + tc->Name() + ")");
                    return -1;
                }
                mx.Set(as.Macro, as.Path);
                ToolPass assemble;
                assemble.Before.push_back("[" + as.Name + "]  " + Paths::FileName(asmFile) + "  ->  " +
                                          PathUtil::MakeRelative(p->ProjectDir(), objFile));
                assemble.Command = mx.Expand(as.Cmd);
                assemble.Options = run;
                assemble.Source = asmFile;
                assemble.ToolName = as.Name;
                assemble.TargetName = tc->Name();
                assemble.Writes = objFile;
                assemble.Step = "assemble";
                if (emulated) assemble.Refused = as.Name + " refused the assembly - the emulator would run it, but it is not a TI program";
                step->Passes.push_back(assemble);
            }
            steps.erase(std::remove_if(steps.begin(), steps.end(),
                                       [](const CompileStepPtr& s) { return s->Passes.empty(); }), steps.end());
            if (RunSteps(steps, p->ProjectDir()) != 0) return -1;
        }
        else
        {
            for (const std::string& src : sources)
                objects.push_back(Paths::Combine(objDir, stems[src] + tc->ObjExt));
            if (unit.size() > 1)
            {
                for (const std::string& s : unit) RemoveFirst(objects, Paths::Combine(objDir, stems[s] + tc->ObjExt));
                objects.push_back(Paths::Combine(objDir, ProgramStem(p, cfg) + tc->ObjExt));
                shalimarProgram = true;
            }
        }

        if (crossTarget)
        {
            Say(RideToolchain::WhyNotRun(crossTool, t));
            Say("[assembly written to " + PathUtil::MakeRelative(p->ProjectDir(), objDir) + " - nothing here assembles or links it]");
            return 1;
        }
        if (r->Action == BuildAction::CompileFile || r->Action == BuildAction::AssembleOnly) return 1;
        if (emulated && p->EmulateOnly)
        {
            Say("[no .out: the project says emulateOnly - Run is Emulate on vm6747]");
            summary->Program.clear();
            return 1;
        }
        if (objects.empty())
        {
            Report(Severity::Warning, std::string(), "project " + p->Name + " has no source files to build");
            return 1;
        }
        if (StringUtil::Contains(p->Template, "Library"))
        {
            Say("Library project: objects are kept in " + PathUtil::MakeRelative(p->ProjectDir(), objDir));
            return anyWork ? 1 : 0;
        }

        // ---- link ----
        std::string program = ProgramPath(p, cfg);
        bool linkUpToDate = !anyWork && r->Action == BuildAction::Build && Paths::IsFile(program);
        if (linkUpToDate)
        {
            long long programTime = WriteTime(program);
            for (const std::string& o : objects)
                if (Paths::IsFile(o) && WriteTime(o) > programTime) linkUpToDate = false;
        }
        if (linkUpToDate)
        {
            Say(p->Name + " is up to date.");
            summary->Program = program;
            return 0;
        }

        // cl's objects (a part the project or a folder gives to cl) are Microsoft's: its link.exe links them
        bool clObjects = false, otherObjects = false;
        PartsOf(p, sources, clObjects, otherObjects);
        bool clangCodeView = codeView || (debug && !emulated && !native && !codeViewMissing && !ownCodeView &&
                                          NeedsClang(p, cfg, t) && RideToolchain::DebugsWithCodeView(RideTool::C90, t, debug));
        Step ld = LinkerFor(tc, t, debug, native, clangCodeView, clObjects && !emulated);
        // the PDB of GRIDE's own link: /DEBUG, which it writes beside the program
        if (ownCodeView && !clangCodeView && !emulated && ld.Role == "linker")
        {
            std::string flags = mx.Get("LinkFlags");
            if (StringUtil::IndexOfIgnoreCase(flags, "/DEBUG", 0) < 0) mx.Set("LinkFlags", StringUtil::Trim(flags + " /DEBUG"));
        }
        // Linux and macOS: the linker role is the C driver (cc), which leaves the C++ runtime out. C++ and
        // Shalimar objects are linked by the target's host C++ driver (c++) with the same arguments, as cpp11
        // and shalimar link their own programs on these hosts (new in GRIDEm: GRIDEd links only Windows and
        // the C6747 itself)
        if (!emulated && (t == Target::MacOS || t == Target::Linux) && ld.Role == "linker" && NeedsCxxRuntime(p, sources))
        {
            ToolDefinitionPtr cxx = ToolchainDefinitions::Current()->ToolFor(t, "hostCompiler");
            std::string cxxPath = cxx ? tc->ResolveFor("hostCompiler", cxx->Exe) : std::string();
            if (Paths::IsFile(cxxPath))
            {
                Say("[C++ objects: linked with " + cxx->Name + ", which brings the C++ runtime]");
                ld.Path = cxxPath;
                ld.Name = cxx->Name;
            }
            else
                Report(Severity::Warning, std::string(), "the host C++ driver (" + std::string(cxx ? cxx->Exe : "c++") +
                       ") was not found - C++ objects linked with " + ld.Name + " lack the C++ runtime");
        }
        // tms6747 with the named linker gone: TI's stands in, and the console says so (RIDE's tiLinker)
        if (emulated && !native && !viaSetup && !Paths::IsFile(ld.Path))
        {
            Step theirs = LinkerFor(tc, t, debug, true, false, false);
            if (theirs.Role == "nativeLinker" && Paths::IsFile(theirs.Path))
            {
                Say("[" + ld.Path + " is not there - linking with TI's " + theirs.Path + "]");
                ld = theirs;
            }
        }
        if (!Paths::IsFile(ld.Path) && !viaSetup)
        {
            Report(Severity::Error, std::string(), ld.Name + " not found: " + ld.Path + "  (looked in " +
                   tc->LookedIn(ld.Role == "linker" ? tc->Linker : ld.Path) + "; set it in Settings > Toolchains > " + tc->Name() + ")");
            return -1;
        }
        mx.Set(ld.Macro, ld.Path);
        // the Windows C runtime: GRIDE's own (its layered libraries and kernel32.lib, by their paths) or Microsoft's
        std::string crt;
        if (!emulated && RideToolchain::HostCrt(t, "grt"))
        {
            crt = WindowsRuntime(clObjects);
            std::string libs;
            bool found = RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot, libs);
            std::string where = RideToolchain::CrtDirectory(t, "grt", tc->ToolDir, ToolchainSettings::RideRoot);
            if (!found && settings->Env->EffectiveWindowsRuntime() == "auto" && VisualStudio::Find())
            {
                Say("[GRIDE's C runtime is not in " + where + " - Microsoft's (Visual Studio's) is linked instead]");
                crt = "msvc";
                found = RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot, libs);
            }
            if (!found)
            {
                Report(Severity::Error, std::string(), "GRIDE's C runtime is not there: grtstart.lib, grtcompat.lib, grtcxx.lib, grtc.lib, grtwin.lib and kernel32.lib are looked for in " +
                       where + " inside GRIDEm");
                failedStep = "link";
                return -1;
            }
            if (crt == "grt") libs += kMicrosoftRuntimeRefused;
            mx.Set("CrtLibs", libs);
            if (crt == "grt" && clObjects)
                Report(Severity::Warning, std::string(), "cl's objects name Microsoft's C runtime, and GRIDE's runtime is chosen (Settings > Projects > GRIDE Paths, Windows C runtime): they may not link");
            if (!crtSaid)
            {
                crtSaid = true;
                std::string named;
                RideToolchain::CrtLibraries(t, debug, crt, tc->ToolDir, ToolchainSettings::RideRoot, named);
                Say(crt == "grt" ? "[GRIDE's C runtime: grtstart, grtcompat, grtcxx, grtc, grtwin and kernel32.lib from " + where + "]"
                                 : "[Microsoft's C runtime: " + named + ", found through LIB]");
            }
        }
        // every object cl's: they name their own C runtime, so no CRT list is added to theirs
        if (!emulated && clObjects && !otherObjects && ld.Role != "linker")
        {
            mx.Set("CrtLibs", std::string());
            Say("[cl's objects name their own C runtime - " + ld.Name + " takes it from them, no CRT list is added]");
        }
        int linked = emulated ? TiLink(p, cfg, tc, mx, run, ld, objects, program, debug, shalimarProgram, native)
                              : HostLink(p, tc, mx, run, ld, objects, program, shalimarProgram || !unit.empty(), debug, crt);
        if (linked != 0) { failedStep = "link"; return -1; }
        summary->Program = program;
        return 1;
    }

    // the host link: "$ linking with <linker>", then "[built <program>]"; a Shalimar program takes its
    // runtime library first, the configuration's one
    int BuildEngine::HostLink(const GrideProjectPtr& p, const ToolchainSettingsPtr& tc, MacroExpander& mx,
                              const ToolRunOptions& run, const Step& ld, std::vector<std::string>& objects,
                              const std::string& program, bool shalimarProgram, bool debug, const std::string& crt)
    {
        if (shalimarProgram)
        {
            // the Shalimar runtime built for the C runtime in force
            LibraryDefinitionPtr lib = ToolchainDefinitions::Current()->LibraryByKind(RideToolchain::Arch(tc->TargetKind), "shalimarRuntime", crt);
            std::string shm = RideToolchain::HostShalimarRuntime(tc->TargetKind, debug, tc->ToolDir, ToolchainSettings::RideRoot, crt);
            if (!shm.empty())
            {
                std::string libs = mx.Get("LibFlags");
                mx.Set("LibFlags", StringUtil::Trim(RideToolchain::Quote(shm) + " " + libs));
                Say("[" + Paths::FileName(shm) + " linked first: the Shalimar runtime]");
            }
            else if (lib)
                Say("[" + lib->FileFor(debug) + " is not inside GRIDEm (" + lib->Directory + ") - the link may miss the Shalimar runtime]");
        }
        mx.Set("Objects", MacroExpander::JoinQuoted(objects));
        Say("$ linking with " + ld.Name);
        Say("[LINK] " + PathUtil::MakeRelative(p->ProjectDir(), program));
        if (RunTool(mx.Expand(ld.Cmd), p->ProjectDir(), run, std::string(), ld.Name, tc->Name(), false) != 0) return -1;
        Say("[built " + program + "]");
        return 0;
    }

    // the C6747 program (RIDE's makeTiProgram): lnk6x against RTS6x
    int BuildEngine::TiLink(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg, const ToolchainSettingsPtr& tc,
                            MacroExpander& mx, const ToolRunOptions& run, Step ld, std::vector<std::string>& objects,
                            const std::string& program, bool debug, bool shalimarProgram, bool native)
    {
        ToolchainDefinitionsPtr defs = ToolchainDefinitions::Current();
        std::string objDir = p->ToAbsolute(cfg->ObjDir);
        std::string runtimeDir = tc->ResolveRuntimeDir();
        const Settings::CcsDefinition& cd = defs->Ccs;
        bool tiNamed = RideToolchain::TiNamed();
        bool rts6x = !tiNamed && !runtimeDir.empty() && Paths::IsDirectory(runtimeDir);
        std::string tiDir = rts6x ? std::string() : RideToolchain::TiDir();
        std::string made = "[" + std::to_string(objects.size()) + " TI objects made] no .out: ";
        if (!rts6x && tiDir.empty())
        {
            LibraryDefinitionPtr rtsLib = defs->RuntimeOf(cfg->TargetKind);
            Report(Severity::Error, std::string(), made + "a tms6747 build links against RTS6x in " +
                   (rtsLib ? rtsLib->Directory : std::string("its directory")) +
                   " inside GRIDEm, or TI's runtime when its compiler directory is named under Settings > Projects > GRIDE Paths, and neither is here");
            return -1;
        }
        std::string tiLib = rts6x ? std::string() : Paths::Combine(tiDir, cd.TiLib);
        std::string tiExtra = rts6x || StringUtil::IsNullOrWhiteSpace(RideToolchain::TiLibDir)
                                  ? std::string() : Platform::Environment::Expand(StringUtil::Trim(RideToolchain::TiLibDir));
        if (!tiExtra.empty() && !Paths::IsDirectory(tiExtra)) tiExtra.clear();
        bool eh = rts6x || Paths::IsFile(Paths::Combine(tiLib, cd.RuntimeLibrary)) ||
                  (!tiExtra.empty() && Paths::IsFile(Paths::Combine(tiExtra, cd.RuntimeLibrary)));
        if (!eh && !tiNamed)
        {
            Report(Severity::Error, std::string(), made + tiDir + " has only " + cd.RuntimeFallback + " - these objects need " +
                   cd.RuntimeLibrary + ", named under Settings > Projects > GRIDE Paths (TI library directory)");
            return -1;
        }
        // the C runtime in force, which every name a project has for it is read as
        std::vector<std::string> libs;
        std::string rts = rts6x ? RideToolchain::RtsLibrary(debug) : eh ? cd.RuntimeLibrary : cd.RuntimeFallback;
        for (const std::string& lib : cfg->Libs)
        {
            if (StringUtil::IsNullOrWhiteSpace(lib)) continue;
            std::string l = StringUtil::Trim(lib);
            if (RideToolchain::IsRtsAlias(l) || StringUtil::EqualsIgnoreCase(l, cd.RuntimeIndex) ||
                StringUtil::EqualsIgnoreCase(l, cd.RuntimeLibrary) || StringUtil::EqualsIgnoreCase(l, cd.RuntimeFallback))
                l = rts;
            if (!Contains(libs, l)) libs.push_back(l);
        }
        if (cfg->Libs.empty() && !rts.empty()) libs.push_back(rts);
        for (const std::string& l : p->Libraries)
            if (!StringUtil::IsNullOrWhiteSpace(l) && !Contains(libs, p->ToAbsolute(StringUtil::Trim(l))))
                libs.push_back(p->ToAbsolute(StringUtil::Trim(l)));
        if (shalimarProgram)
        {
            std::string shm = RideToolchain::ShalimarRtsLibrary(debug);
            bool packed = !shm.empty() && !runtimeDir.empty() && Paths::IsFile(Paths::Combine(runtimeDir, shm));
            if (packed) { RemoveFirst(libs, shm); libs.insert(libs.begin(), shm); }
            else
            {
                // the runtime's assembly, copied in and assembled here rather than inside the app
                std::string theirs = RideToolchain::FindShalimarRuntimeDir(tc->ToolDir, ToolchainSettings::RideRoot, debug);
                if (!theirs.empty())
                {
                    std::string into = Paths::Combine(objDir, "shmrt");
                    PathUtil::EnsureDirectory(into);
                    Step as = AssemblerFor(tc, cfg->TargetKind, debug, native, RideTool::Shalimar);
                    mx.Set(as.Macro, as.Path);
                    for (const std::string& leaf : Paths::ListFiles(theirs))
                    {
                        if (!StringUtil::EqualsIgnoreCase(Paths::Extension(leaf), tc->AsmExt)) continue;
                        std::string s = Paths::Combine(theirs, leaf);
                        std::string copy = Paths::Combine(into, leaf);
                        std::string obj = Paths::ChangeExtension(copy, tc->ObjExt);
                        std::string error;
                        if (!Paths::CopyFile(s, copy, error)) throw std::runtime_error(error);
                        mx.Set("AsmFile", copy);
                        mx.Set("ObjFile", obj);
                        if (RunTool(mx.Expand(as.Cmd), p->ProjectDir(), run, copy, as.Name, tc->Name(), false) != 0)
                        {
                            Say("cannot assemble the Shalimar runtime's " + leaf);
                            return -1;
                        }
                        objects.push_back(obj);
                    }
                }
            }
        }
        // the linker command file: the project's where it is there, else RIDE's flat memory map
        std::string cmdFile = cfg->LinkerCmd.empty() ? std::string() : p->ToAbsolute(cfg->LinkerCmd);
        bool ownCmd = !cmdFile.empty() && Paths::IsFile(cmdFile);
        if (!ownCmd)
        {
            std::string text = RideToolchain::TiLinkCmd();
            std::string name = RideToolchain::TiLinkCmdName();
            if (!text.empty() && !name.empty())
            {
                cmdFile = Paths::Combine(objDir, name);
                std::string error;
                if (!Paths::WriteFile(cmdFile, text, error)) throw std::runtime_error(error);
            }
        }
        mx.Set("LinkerCmd", cmdFile);
        std::vector<std::string> libDirs;
        if (!tiLib.empty()) mx.Set("RuntimeDir", tiLib);      // the link template's -i "$(RuntimeDir)" is TI's lib then
        if (!tiExtra.empty()) libDirs.push_back(tiExtra);
        for (const std::string& d : cfg->LibDirs)
        {
            std::string abs = p->ToAbsolute(d);
            if (Paths::IsDirectory(abs)) libDirs.push_back(abs);
            else Say("[library search path not on this machine, left out: " + d + "]");
        }
        libDirs.insert(libDirs.end(), tc->LibDirs.begin(), tc->LibDirs.end());
        mx.Set("LibDirFlags", MacroExpander::JoinFlags(tc->LibDirFlag, libDirs));
        // TI's linker takes c_int00 from the library only under --rom_model, the model CCS defaults to
        if (!rts6x && ld.Role == "nativeLinker" && !ownCmd && cfg->ExtraLinkFlags.find("_model") == std::string::npos)
            mx.Set("LinkFlags", StringUtil::Trim(cfg->ExtraLinkFlags + " --rom_model"));
        mx.Set("LibFlags", MacroExpander::JoinFlags(tc->LibFlag, libs));
        mx.Set("Objects", MacroExpander::JoinQuoted(objects));
        Say("$ " + ld.Name + " " + std::to_string(objects.size()) + " objects, " + StringUtil::Join(", ", libs) +
            (ownCmd ? ", " + Paths::FileName(cmdFile) : std::string()) + " -o " + Paths::FileName(program));
        if (RunTool(mx.Expand(ld.Cmd), p->ProjectDir(), run, std::string(), ld.Name, tc->Name(), false) != 0)
        {
            Say(ld.Name + " did not link it - see its messages above");
            return -1;
        }
        Say("[linked " + program + "]");
        return 0;
    }

    std::vector<std::string> BuildEngine::Leaves(const std::vector<std::string>& files)
    {
        std::vector<std::string> r;
        for (const std::string& f : files) r.push_back(Paths::FileName(f));
        return r;
    }

    // the options every tool of the target runs with: the user's variables, and on the host target
    // Visual Studio's directories (none on the Mac)
    ToolRunOptions BuildEngine::RunOptions(const ToolchainSettingsPtr& tc, Target t)
    {
        ToolRunOptions o;
        o.SetupScript = tc->SetupScript;
        o.Environment = tc->EnvironmentVars;
        TargetDefinitionPtr td = ToolchainDefinitions::Current()->TargetOf(t);
        if (td && td->Host && StringUtil::IsNullOrWhiteSpace(o.SetupScript))
        {
            std::vector<std::string> vs = VisualStudio::EnvironmentLines();
            o.Environment.insert(o.Environment.end(), vs.begin(), vs.end());
        }
        o.OutputCodePage = tc->OutputCodePage;
        return o;
    }

    // the environment RIDE sets before a compiler runs (prepareFor): each compiler's *_AS and *_LD
    // name the project's assembler and linker, empty for a Debug build carrying CodeView
    ToolRunOptions BuildEngine::CompileOptions(const ToolRunOptions& base, const ToolchainSettingsPtr& tc, bool codeView,
                                               const std::vector<std::string>& optionEnvironment)
    {
        ToolRunOptions o = base;
        o.Environment.insert(o.Environment.end(), optionEnvironment.begin(), optionEnvironment.end());
        std::string as = codeView || StringUtil::IsNullOrWhiteSpace(tc->Assembler) ? std::string() : tc->ResolveFor("assembler", tc->Assembler);
        std::string ld = codeView || StringUtil::IsNullOrWhiteSpace(tc->Linker) ? std::string() : tc->ResolveFor("linker", tc->Linker);
        for (const ToolDefinitionPtr& d : ToolchainDefinitions::Current()->Tools)
        {
            if (d->Role != "cCompiler" && d->Role != "cppCompiler" && d->Role != "shalimarCompiler") continue;
            for (Settings::StringMap::const_iterator kv = d->Environment.begin(); kv != d->Environment.end(); ++kv)
            {
                if (StringUtil::EndsWith(kv->first, "_AS")) o.Environment.push_back(kv->first + "=" + as);
                else if (StringUtil::EndsWith(kv->first, "_LD")) o.Environment.push_back(kv->first + "=" + ld);
            }
        }
        return o;
    }

    // the macros that depend on the compiler a file goes to
    void BuildEngine::SetCompilerMacros(MacroExpander& mx, RideTool tool, Language lang, const GrideProjectPtr& p,
                                        const BuildConfigurationPtr& cfg, const ToolchainSettingsPtr& tc)
    {
        bool debug = cfg->IsDebug();
        // no clang: the Release spelling (-masm=masm) and no -g, which only clang's assembly carries
        bool noLineTable = debug && codeViewMissing && RideToolchain::DebugsWithCodeView(tool, cfg->TargetKind, true) &&
                           !RideToolchain::OwnCodeView(tool, cfg->TargetKind, true);
        std::string flags = RideToolchain::ConfigFlags(tool, debug, cfg->TargetKind, p->Options);
        if (noLineTable)
        {
            std::vector<std::string> kept;
            for (const std::string& f : StringUtil::SplitNonEmpty(flags, " "))
                if (f != "-g") kept.push_back(f);
            flags = StringUtil::Join(" ", kept);
        }
        mx.Set("ArchFlag", RideToolchain::ArchFlag(tool, cfg->TargetKind));
        mx.Set("ConfigFlags", flags);
        mx.Set("AsmSyntaxFlag", RideToolchain::AssemblerFlag(tool, cfg->TargetKind, debug && !noLineTable,
                                                             !StringUtil::IsNullOrWhiteSpace(tc->Assembler)));
        mx.Set("LangFlags", RideToolchain::LanguageFlags(tool, lang));
        // the compiler's own spellings; the target's settings stand for the C compiler's family,
        // and a compiler with no include flag (shalimar) takes no -I and no -D
        std::string ownInclude = RideToolchain::IncludeFlagOf(tool);
        std::string ownDefine = RideToolchain::DefineFlagOf(tool);
        bool hostTool = tool == RideTool::Cl || tool == RideTool::HostCxx;
        std::string includeFlag = hostTool ? ownInclude : tc->IncludeFlag;
        std::string defineFlag = hostTool ? ownDefine : tc->DefineFlag;
        std::vector<std::string> inc;
        for (const std::string& d : cfg->IncludeDirs) inc.push_back(p->ToAbsolute(d));
        for (const std::string& d : p->Includes)
            if (!StringUtil::IsNullOrWhiteSpace(d)) inc.push_back(p->ToAbsolute(StringUtil::Trim(d)));
        inc.insert(inc.end(), tc->IncludeDirs.begin(), tc->IncludeDirs.end());
        for (const std::string& d : settings->Env->SharedIncludes)
            if (!StringUtil::IsNullOrWhiteSpace(d)) inc.push_back(Platform::Environment::Expand(StringUtil::Trim(d)));
        for (const std::string& d : RideToolchain::InstallIncludeDirs(tool, ToolchainSettings::RideRoot,
                                                                      settings->Env->RideIncludeDir, settings->Env->RideLibDir))
            if (!Contains(inc, d)) inc.push_back(d);
        // the host's compiler reads its own headers: an installation's header directory would stand in front of them
        if (hostTool) KeepInstallHeadersFrom(inc);
        mx.Set("IncludeFlags", ownInclude.empty() ? std::string() : MacroExpander::JoinFlags(includeFlag, inc));
        mx.Set("DefineFlags", ownDefine.empty() ? std::string() : MacroExpander::JoinDefines(defineFlag, cfg->Defines));
        // a Visual Studio project's own cl settings go to cl after the configuration's flags
        std::string own;
        if (tool == RideTool::Cl && lang != Language::Assembly)
        {
            std::vector<std::string> unmapped;
            own = ProjectToolFlags(p, cfg, RideToolchain::Definition(tool), &unmapped);
            if (!clSettingsSaid && (!own.empty() || !unmapped.empty()))
            {
                clSettingsSaid = true;
                if (!own.empty()) Say("[the project's own settings give cl: " + own + "]");
                if (!unmapped.empty())
                    Say("[not given to cl - toolchain.json's projectSettings names no switch for: " + StringUtil::Join("; ", unmapped) + "]");
            }
        }
        mx.Set("CompileFlags", StringUtil::Trim(cfg->ExtraCompileFlags + " " + own));
    }

    // takes the installation's header directories out of a host compiler's list, and says so once
    void BuildEngine::KeepInstallHeadersFrom(std::vector<std::string>& inc)
    {
        const Settings::InstallationDefinition& inst = ToolchainDefinitions::Current()->Installation;
        std::vector<std::string> ride;
        const std::string candidates[] = {
            RideToolchain::InstallDir(ToolchainSettings::RideRoot, inst.IncludeDir, settings->Env->RideIncludeDir),
            RideToolchain::InstallDir(ToolchainSettings::RideRoot, inst.LibDir, settings->Env->RideLibDir) };
        for (const std::string& d : candidates)
            if (!d.empty()) ride.push_back(d);
        std::vector<std::string> kept;
        for (int i = (int)inc.size() - 1; i >= 0; i--)
        {
            bool theirs = false;
            for (const std::string& d : ride)
                if (PathUtil::SamePath(inc[(size_t)i], d)) theirs = true;
            if (!theirs) continue;
            if (!Contains(kept, inc[(size_t)i])) kept.insert(kept.begin(), inc[(size_t)i]);
            inc.erase(inc.begin() + i);
        }
        if (kept.empty() || rideHeadersSaid) return;
        rideHeadersSaid = true;
        Say("[" + StringUtil::Join(" and ", kept) + (kept.size() == 1 ? " is" : " are") +
            " not given to cl: the installation's headers would stand in front of Visual Studio's]");
    }

    MacroExpander BuildEngine::BaseMacros(const GrideProjectPtr& p, const BuildConfigurationPtr& cfg,
                                          const ToolchainSettingsPtr& tc, const BuildRequestPtr& r)
    {
        MacroExpander mx;
        mx.Set("ToolDir", tc->ToolDir);
        mx.Set("Assembler", tc->ResolveFor("assembler", tc->Assembler));
        mx.Set("Linker", tc->ResolveFor("linker", tc->Linker));
        mx.Set("DebugAssembler", tc->ResolveFor("debugAssembler", tc->DebugAssembler));
        mx.Set("DebugLinker", tc->ResolveFor("debugLinker", tc->DebugLinker));
        mx.Set("Runtime", tc->Resolve(tc->Runtime));
        // RTS6x is the TMS320C6747's runtime alone: no other target is given its directory
        mx.Set("RuntimeDir", RideToolchain::IsEmulated(cfg->TargetKind) ? tc->ResolveRuntimeDir() : std::string());
        mx.Set("Simulator", tc->ResolveFor("simulator", tc->Simulator));
        mx.Set("Emulator", tc->ResolveFor("emulator", tc->Emulator));
        mx.Set("Debugger", tc->ResolveFor("debugger", tc->Debugger));
        mx.Set("Arch", RideToolchain::Arch(cfg->TargetKind));
        mx.Set("ArchFlag", RideToolchain::ArchFlag(RideTool::C90, cfg->TargetKind));
        mx.Set("ConfigFlags", RideToolchain::ConfigFlags(RideTool::C90, cfg->IsDebug(), cfg->TargetKind, p->Options));
        mx.Set("CrtLibs", RideToolchain::CrtLibraries(cfg->TargetKind, cfg->IsDebug()));
        mx.Set("Config", cfg->Config);
        mx.Set("Target", TargetInfo::Name(cfg->TargetKind));
        mx.Set("ProjectDir", p->ProjectDir());
        mx.Set("ProjectName", p->Name);
        mx.Set("WorkspaceDir", r->WorkspaceDir);
        mx.Set("ObjDir", p->ToAbsolute(cfg->ObjDir));
        mx.Set("OutDir", p->ToAbsolute(cfg->OutDir));
        std::string program = ProgramPath(p, cfg);
        mx.Set("Output", program);
        mx.Set("Program", program);
        mx.Set("Optimise", cfg->Optimise);
        mx.Set("CompileFlags", cfg->ExtraCompileFlags);
        mx.Set("LinkFlags", cfg->ExtraLinkFlags);
        mx.Set("LinkerCmd", cfg->LinkerCmd.empty() ? std::string() : p->ToAbsolute(cfg->LinkerCmd));

        std::vector<std::string> inc;
        for (const std::string& d : cfg->IncludeDirs) inc.push_back(p->ToAbsolute(d));
        for (const std::string& d : p->Includes)
            if (!StringUtil::IsNullOrWhiteSpace(d)) inc.push_back(p->ToAbsolute(StringUtil::Trim(d)));
        inc.insert(inc.end(), tc->IncludeDirs.begin(), tc->IncludeDirs.end());
        mx.Set("IncludeFlags", MacroExpander::JoinFlags(tc->IncludeFlag, inc));
        mx.Set("DefineFlags", MacroExpander::JoinDefines(tc->DefineFlag, cfg->Defines));

        std::vector<std::string> libDirs;
        for (const std::string& d : cfg->LibDirs) libDirs.push_back(p->ToAbsolute(d));
        libDirs.insert(libDirs.end(), tc->LibDirs.begin(), tc->LibDirs.end());
        mx.Set("LibDirFlags", MacroExpander::JoinFlags(tc->LibDirFlag, libDirs));
        // the project's libraries, then the shared ones every host link takes
        std::vector<std::string> libs;
        for (const std::string& l : cfg->Libs)
        {
            // RTS6x's libraries belong to the TMS320C6747 alone
            if (!RideToolchain::IsEmulated(cfg->TargetKind) && IsC6747Library(l))
            {
                Say("[" + l + " is the TMS320C6747's runtime - left out of the " + TargetInfo::Name(cfg->TargetKind) + " link]");
                continue;
            }
            libs.push_back(l);
        }
        for (const std::string& l : p->Libraries)
            if (!StringUtil::IsNullOrWhiteSpace(l)) libs.push_back(p->ToAbsolute(StringUtil::Trim(l)));
        if (!RideToolchain::IsEmulated(cfg->TargetKind))
            for (const std::string& l : settings->Env->SharedLibraries)
                if (!StringUtil::IsNullOrWhiteSpace(l) && !Contains(libs, StringUtil::Trim(l)))
                    libs.push_back(Platform::Environment::Expand(StringUtil::Trim(l)));
        mx.Set("LibFlags", MacroExpander::JoinFlags(tc->LibFlag, libs));
        return mx;
    }

    // a library of the TMS320C6747's runtime, by its file name: ^(rts6|printf6x|shmrt6x), ignoring case
    bool BuildEngine::IsC6747Library(const std::string& lib)
    {
        if (StringUtil::IsNullOrWhiteSpace(lib)) return false;
        std::string n = StringUtil::Trim(lib);
        while (!n.empty() && n[0] == '"') n.erase(0, 1);
        while (!n.empty() && n[n.size() - 1] == '"') n.erase(n.size() - 1);
        n = Paths::FileName(PathUtil::Normalize(n));
        return StringUtil::StartsWithIgnoreCase(n, "rts6") || StringUtil::StartsWithIgnoreCase(n, "printf6x") ||
               StringUtil::StartsWithIgnoreCase(n, "shmrt6x");
    }

    long long BuildEngine::NewestHeader(const GrideProjectPtr& p) const
    {
        long long newest = kMinTime;
        for (const std::string& f : p->AllFiles())
        {
            if (!IsHeader(f) || !Paths::IsFile(f)) continue;
            long long t = WriteTime(f);
            if (t > newest) newest = t;
        }
        return newest;
    }

    // Clean deletes every file in the folder, so it must lie strictly inside the project folder
    bool BuildEngine::CanClean(const GrideProjectPtr& p, const std::string& dir, const std::string& what)
    {
        if (!Paths::IsDirectory(dir) || PathUtil::IsStrictlyUnder(p->ProjectDir(), dir)) return true;
        Report(Severity::Error, std::string(), "Clean refused: the " + what + " folder '" + dir +
               "' is not a sub-folder of the project folder '" + p->ProjectDir() + "'. "
               "Nothing was deleted. Change it in Project > Properties.");
        return false;
    }

    void BuildEngine::CleanDir(const std::string& dir)
    {
        if (!Paths::IsDirectory(dir)) return;
        int n = 0;
        for (const std::string& leaf : Paths::ListFiles(dir))
            if (Paths::DeleteFile(Paths::Combine(dir, leaf))) n++;
        Say("Cleaned " + std::to_string(n) + " file(s) in " + dir);
    }
}}
