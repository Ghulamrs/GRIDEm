// =============================================================================
//  ToolVersions.cpp - see ToolVersions.h
// =============================================================================
#include "ToolVersions.h"
#include "../Core/StringUtil.h"
#include "../Platform/CommandLine.h"
#include "../Platform/Paths.h"
#include "../Platform/Process.h"
#include <cstdio>
#include <mutex>

namespace GRIDEm { namespace Services {

    using Core::StringUtil;
    using Platform::Paths;

    const char* ToolVersions::IdeVersion = "1.5.0";
    const char* ToolVersions::Author = "\xC2\xA9" "2026 G. R. Akhtar";

    std::vector<std::string> ToolVersions::Names()
    {
        return std::vector<std::string>{ "c90", "cpp11", "shalimar", "c2s", "asm6x", "lnk6x", "sim6747", "vm6747", "masm", "link" };
    }

    // (?<![\w.])(\d+\.\d+(?:\.\d+)?)(?![\w.]) by hand: where it starts and how long it is
    static bool FindVersion(const std::string& line, size_t& at, size_t& length)
    {
        for (size_t i = 0; i < line.size(); i++)
        {
            if (!StringUtil::IsDigit(line[i])) continue;
            if (i > 0 && (StringUtil::IsIdentifierChar(line[i - 1]) || line[i - 1] == '.')) continue;
            size_t j = i;
            while (j < line.size() && StringUtil::IsDigit(line[j])) j++;
            if (j >= line.size() || line[j] != '.' || j + 1 >= line.size() || !StringUtil::IsDigit(line[j + 1])) continue;
            j++;
            while (j < line.size() && StringUtil::IsDigit(line[j])) j++;
            size_t end = j;
            if (j + 1 < line.size() && line[j] == '.' && StringUtil::IsDigit(line[j + 1]))
            {
                size_t k = j + 1;
                while (k < line.size() && StringUtil::IsDigit(line[k])) k++;
                if (!(k < line.size() && (StringUtil::IsIdentifierChar(line[k]) || line[k] == '.'))) end = k;
            }
            if (end < line.size() && (StringUtil::IsIdentifierChar(line[end]) || line[end] == '.')) continue;
            at = i;
            length = end - i;
            return true;
        }
        return false;
    }

    ToolVersion ToolVersions::Ask(const std::string& dir, const std::string& name)
    {
        ToolVersion v;
        v.Name = name;
        std::string exe = Paths::Combine(dir, name);
        if (!Paths::IsExecutable(exe)) exe = Paths::Combine(dir, name + ".exe");
        if (!Paths::IsExecutable(exe)) { v.Description = "not found beside GRIDEm"; return v; }
        const char* tries[] = { "--version", "" };
        for (const char* args : tries)
        {
            std::vector<std::string> lines = StringUtil::SplitNonEmpty(Run(exe, args), "\n");
            for (const std::string& raw : lines)
            {
                std::string line = Own(StringUtil::Trim(raw));
                if (line.empty() || IsComplaint(line)) continue;
                size_t at = 0, length = 0;
                bool found = FindVersion(line, at, length);
                if (v.Version.empty() && found) v.Version = line.substr(at, length);
                if (v.Description.empty() && !StringUtil::StartsWithIgnoreCase(line, "Version"))
                    v.Description = Tidy(name, found ? line.substr(0, at) + line.substr(at + length) : line);
            }
            if (!v.Version.empty()) break;
        }
        // a program that does not say its version goes with the IDE's (masm, link)
        if (v.Version.empty())
        {
            v.Version = IdeVersion;
            v.Description = name == "masm" ? "x64 assembler - GRIDE's version"
                          : name == "link" ? "x64 linker - GRIDE's version"
                          : "GRIDE's version";
        }
        return v;
    }

    static std::string PadRight(const std::string& s, size_t width)
    {
        if (s.size() >= width) return s;
        return s + std::string(width - s.size(), ' ');
    }

    std::string ToolVersions::Text(const std::string& dir, const std::string& settingsPath)
    {
        std::string sb;
        sb += std::string("Version ") + IdeVersion + "\n";
        sb += std::string(Author) + "\n\n";
        sb += "C90, C++11 and Shalimar for macOS and the TMS320C6747.\n\n";
        sb += "Toolchain - as each program reports itself\n";
        for (const std::string& n : Names())
        {
            ToolVersion v = Ask(dir, n);
            sb += "  " + PadRight(v.Name, 9) + " " + PadRight(v.Version.empty() ? "-" : v.Version, 6) + " " + v.Description + "\n";
        }
        sb += "\nGRIDEm for macOS - C++14 and AppKit\n";
        if (!settingsPath.empty()) sb += "Settings: " + settingsPath + "\n";
        return StringUtil::TrimEnd(sb);
    }

    std::string ToolVersions::Run(const std::string& exe, const std::string& args)
    {
        std::vector<std::string> argv;
        argv.push_back(exe);
        std::vector<std::string> rest = Platform::CommandLine::Split(args);
        argv.insert(argv.end(), rest.begin(), rest.end());
        std::string out, err;
        std::mutex lock;
        Platform::Process p;
        p.OnOutput = [&out, &lock](const std::string& b) { std::lock_guard<std::mutex> h(lock); out += b; };
        p.OnError = [&err, &lock](const std::string& b) { std::lock_guard<std::mutex> h(lock); err += b; };
        std::string why;
        if (!p.Start(argv, std::string(), std::vector<std::string>(), Platform::ProcessMode::Pipes, true, why))
            return std::string();
        if (!p.Wait(3000)) { p.Kill(); p.Wait(1000); return std::string(); }
        std::lock_guard<std::mutex> h(lock);
        return out + "\n" + err;
    }

    // "(c)2026 G. R. Akhtar - Shalimar 1.3" -> "Shalimar 1.3"
    std::string ToolVersions::Own(const std::string& line)
    {
        size_t dash = line.find(" - ");
        size_t who = line.find("Akhtar");
        if (dash != std::string::npos && dash > 0 && who != std::string::npos && who < dash)
            return StringUtil::Trim(line.substr(dash + 3));
        return line;
    }

    bool ToolVersions::IsComplaint(const std::string& line)
    {
        return StringUtil::StartsWithIgnoreCase(line, "usage") || StringUtil::ContainsIgnoreCase(line, "unknown")
            || StringUtil::ContainsIgnoreCase(line, "no file matches") || StringUtil::ContainsIgnoreCase(line, "not one this");
    }

    static std::string TrimStartAny(const std::string& s, const std::string& chars)
    {
        size_t i = 0;
        while (i < s.size() && chars.find(s[i]) != std::string::npos) i++;
        return s.substr(i);
    }

    static std::string TrimEndAny(const std::string& s, const std::string& chars)
    {
        size_t n = s.size();
        while (n > 0 && chars.find(s[n - 1]) != std::string::npos) n--;
        return s.substr(0, n);
    }

    std::string ToolVersions::Tidy(const std::string& name, const std::string& text)
    {
        // \s+([,)]) -> $1
        std::string s;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (StringUtil::IsSpace(text[i]))
            {
                size_t j = i;
                while (j < text.size() && StringUtil::IsSpace(text[j])) j++;
                if (j < text.size() && (text[j] == ',' || text[j] == ')')) { i = j - 1; continue; }
            }
            s += text[i];
        }
        // \s{2,} -> " "
        std::string t;
        for (size_t i = 0; i < s.size(); i++)
        {
            if (StringUtil::IsSpace(s[i]) && i + 1 < s.size() && StringUtil::IsSpace(s[i + 1]))
            {
                size_t j = i;
                while (j < s.size() && StringUtil::IsSpace(s[j])) j++;
                t += ' ';
                i = j - 1;
                continue;
            }
            t += s[i];
        }
        s = StringUtil::Trim(t);
        if (StringUtil::StartsWithIgnoreCase(s, name) && s.size() > name.size() && !StringUtil::IsLetterOrDigit(s[name.size()]))
            s = TrimStartAny(s.substr(name.size()), " ,-");
        s = TrimEndAny(StringUtil::Trim(s), ",- ");
        if (s.size() >= 2 && s[0] == '(' && s[s.size() - 1] == ')') s = s.substr(1, s.size() - 2);
        if (StringUtil::StartsWith(s, "- ")) s = s.substr(2);
        return s.empty() ? name : s;
    }
}}
