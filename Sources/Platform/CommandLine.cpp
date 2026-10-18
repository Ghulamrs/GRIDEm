// =============================================================================
//  CommandLine.cpp - see CommandLine.h
// =============================================================================
#include "CommandLine.h"

namespace GRIDEm { namespace Platform {

    static std::string TrimBlanks(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
        return s.substr(b, e - b);
    }

    void CommandLine::SplitCommand(const std::string& line, std::string& file, std::string& args)
    {
        std::string cmd = TrimBlanks(line);
        if (!cmd.empty() && cmd[0] == '"')
        {
            size_t end = cmd.find('"', 1);
            if (end == std::string::npos)
            {
                file = cmd.substr(1);
                if (!file.empty() && file[file.size() - 1] == '"') file.erase(file.size() - 1);
                args.clear();
                return;
            }
            file = cmd.substr(1, end - 1);
            args = TrimBlanks(cmd.substr(end + 1));
            return;
        }
        size_t sp = cmd.find(' ');
        if (sp == std::string::npos) { file = cmd; args.clear(); return; }
        file = cmd.substr(0, sp);
        args = TrimBlanks(cmd.substr(sp + 1));
    }

    std::vector<std::string> CommandLine::Split(const std::string& line)
    {
        std::vector<std::string> words;
        std::string cur;
        bool inWord = false, inQuotes = false;
        size_t i = 0;
        while (i < line.size())
        {
            char c = line[i];
            if (c == '\\')
            {
                size_t n = 0;
                while (i < line.size() && line[i] == '\\') { n++; i++; }
                if (i < line.size() && line[i] == '"')
                {
                    cur.append(n / 2, '\\');
                    if (n % 2 == 1) { cur += '"'; i++; }
                }
                else cur.append(n, '\\');
                inWord = true;
                continue;
            }
            if (c == '"')
            {
                // "" inside quotes is a quote, as Microsoft's reader takes it
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i += 2; continue; }
                inQuotes = !inQuotes;
                inWord = true;
                i++;
                continue;
            }
            if ((c == ' ' || c == '\t') && !inQuotes)
            {
                if (inWord) { words.push_back(cur); cur.clear(); inWord = false; }
                i++;
                continue;
            }
            cur += c;
            inWord = true;
            i++;
        }
        if (inWord) words.push_back(cur);
        return words;
    }

    std::string CommandLine::QuoteWord(const std::string& word)
    {
        if (!word.empty() && word.find_first_of(" \t\"") == std::string::npos) return word;
        std::string out = "\"";
        size_t backslashes = 0;
        for (char c : word)
        {
            if (c == '\\') { backslashes++; continue; }
            if (c == '"') { out.append(backslashes * 2 + 1, '\\'); out += '"'; }
            else { out.append(backslashes, '\\'); out += c; }
            backslashes = 0;
        }
        out.append(backslashes * 2, '\\');
        out += '"';
        return out;
    }

    std::string CommandLine::Join(const std::vector<std::string>& words)
    {
        std::string r;
        for (size_t i = 0; i < words.size(); i++)
        {
            if (i > 0) r += ' ';
            r += QuoteWord(words[i]);
        }
        return r;
    }
}}
