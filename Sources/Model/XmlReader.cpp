// =============================================================================
//  XmlReader.cpp - see XmlReader.h
// =============================================================================
#include "XmlReader.h"
#include "../Core/StringUtil.h"
#include "../Core/TextFile.h"
#include "../Platform/Paths.h"
#include <cstdlib>

namespace GRIDEm { namespace Model {

    using Core::StringUtil;

    // ---- XmlNode --------------------------------------------------------------
    std::string XmlNode::LocalName() const
    {
        size_t colon = Name.find(':');
        return colon == std::string::npos ? Name : Name.substr(colon + 1);
    }

    std::string XmlNode::Attr(const std::string& name) const
    {
        for (const std::pair<std::string, std::string>& a : Attributes)
            if (a.first == name) return a.second;
        return std::string();
    }

    bool XmlNode::HasAttr(const std::string& name) const
    {
        for (const std::pair<std::string, std::string>& a : Attributes)
            if (a.first == name) return true;
        return false;
    }

    std::string XmlNode::InnerText() const
    {
        std::string out;
        for (const std::pair<int, std::string>& p : pieces)
        {
            if (p.first < 0) out += p.second;
            else out += Children[(size_t)p.first]->InnerText();
        }
        return out;
    }

    XmlNodePtr XmlNode::Child(const std::string& localName) const
    {
        for (const XmlNodePtr& c : Children)
            if (StringUtil::EqualsIgnoreCase(c->LocalName(), localName)) return c;
        return nullptr;
    }

    const XmlNode* XmlNode::Root() const
    {
        const XmlNode* at = this;
        while (at->parent != nullptr) at = at->parent;
        return at;
    }

    // a step is a name ("*" for any); deep means it may be found at any depth below
    struct PathStep
    {
        std::string name;
        bool deep;
    };

    static void Descend(const XmlNode& from, const PathStep& step, std::vector<XmlNodePtr>& out)
    {
        for (const XmlNodePtr& c : from.Children)
        {
            if (step.name == "*" || c->Name == step.name || c->LocalName() == step.name) out.push_back(c);
            if (step.deep) Descend(*c, step, out);
        }
    }

    static std::vector<PathStep> ParsePath(const std::string& path, bool& fromRoot)
    {
        std::vector<PathStep> steps;
        std::string p = path;
        fromRoot = false;
        if (StringUtil::StartsWith(p, ".//")) p = p.substr(1);           // "//a" relative to here
        else if (StringUtil::StartsWith(p, "./")) p = p.substr(2);
        else if (!p.empty() && p[0] == '/') fromRoot = true;
        size_t i = 0;
        bool deep = false;
        while (i < p.size())
        {
            if (p[i] == '/')
            {
                if (i + 1 < p.size() && p[i + 1] == '/') { deep = true; i += 2; }
                else { deep = false; i += 1; }
                continue;
            }
            size_t j = p.find('/', i);
            if (j == std::string::npos) j = p.size();
            PathStep s;
            s.name = p.substr(i, j - i);
            s.deep = deep;
            steps.push_back(s);
            deep = false;
            i = j;
        }
        return steps;
    }

    // the nodes the steps reach from a context: the context's children for the first step
    static std::vector<XmlNodePtr> Walk(const XmlNode& context, const std::vector<PathStep>& steps)
    {
        std::vector<const XmlNode*> current;
        current.push_back(&context);
        std::vector<XmlNodePtr> found;
        for (size_t k = 0; k < steps.size(); k++)
        {
            found.clear();
            for (const XmlNode* n : current) Descend(*n, steps[k], found);
            current.clear();
            for (const XmlNodePtr& f : found) current.push_back(f.get());
        }
        return found;
    }

    std::vector<XmlNodePtr> XmlNode::SelectNodes(const std::string& path) const
    {
        bool fromRoot = false;
        std::vector<PathStep> steps = ParsePath(path, fromRoot);
        if (!fromRoot) return Walk(*this, steps);
        // from the document: the root element is the first step's own match
        const XmlNode* root = Root();
        XmlNode holder;
        holder.Children.push_back(std::shared_ptr<XmlNode>(const_cast<XmlNode*>(root), [](XmlNode*) {}));
        return Walk(holder, steps);
    }

    XmlNodePtr XmlNode::SelectSingleNode(const std::string& path) const
    {
        std::vector<XmlNodePtr> r = SelectNodes(path);
        return r.empty() ? nullptr : r[0];
    }

    // ---- the parser -------------------------------------------------------------
    class XmlParser
    {
    public:
        explicit XmlParser(const std::string& s) : src(s), pos(0) {}

        XmlNodePtr ParseDocument()
        {
            if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB
                && (unsigned char)src[2] == 0xBF)
                pos = 3;
            XmlNodePtr root;
            while (pos < src.size())
            {
                SkipBlanks();
                if (pos >= src.size()) break;
                if (StartsHere("<?")) { SkipPast("?>"); continue; }
                if (StartsHere("<!--")) { SkipPast("-->"); continue; }
                if (StartsHere("<!DOCTYPE")) { SkipDoctype(); continue; }
                if (src[pos] == '<')
                {
                    if (root) Fail("a second root element");
                    root = ParseElement(nullptr);
                    continue;
                }
                Fail("text outside the root element");
            }
            return root;
        }

    private:
        const std::string& src;
        size_t pos;

        [[noreturn]] void Fail(const std::string& msg) const
        {
            int line = 1;
            for (size_t i = 0; i < pos && i < src.size(); i++)
                if (src[i] == '\n') line++;
            throw XmlException(msg + " (line " + std::to_string(line) + ")");
        }

        bool StartsHere(const char* s) const { return src.compare(pos, std::char_traits<char>::length(s), s) == 0; }

        void SkipBlanks()
        {
            while (pos < src.size() && StringUtil::IsSpace(src[pos])) pos++;
        }

        void SkipPast(const char* end)
        {
            size_t at = src.find(end, pos);
            if (at == std::string::npos) Fail(std::string("missing '") + end + "'");
            pos = at + std::char_traits<char>::length(end);
        }

        void SkipDoctype()
        {
            int depth = 0;
            while (pos < src.size())
            {
                char c = src[pos++];
                if (c == '[') depth++;
                else if (c == ']') depth--;
                else if (c == '>' && depth <= 0) return;
            }
            Fail("unterminated DOCTYPE");
        }

        static bool IsNameChar(char c)
        {
            return StringUtil::IsLetterOrDigit(c) || c == '_' || c == ':' || c == '-' || c == '.';
        }

        std::string ParseName()
        {
            size_t start = pos;
            while (pos < src.size() && IsNameChar(src[pos])) pos++;
            if (pos == start) Fail("a name was expected");
            return src.substr(start, pos - start);
        }

        static void AppendUtf8(std::string& out, unsigned long cp)
        {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000)
            {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        // text with its entities replaced
        std::string Decode(const std::string& raw)
        {
            std::string out;
            size_t i = 0;
            while (i < raw.size())
            {
                if (raw[i] != '&') { out += raw[i++]; continue; }
                size_t semi = raw.find(';', i);
                if (semi == std::string::npos) { out += raw[i++]; continue; }
                std::string e = raw.substr(i + 1, semi - i - 1);
                if (e == "lt") out += '<';
                else if (e == "gt") out += '>';
                else if (e == "amp") out += '&';
                else if (e == "quot") out += '"';
                else if (e == "apos") out += '\'';
                else if (!e.empty() && e[0] == '#')
                {
                    unsigned long cp = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X'))
                                     ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                     : std::strtoul(e.c_str() + 1, nullptr, 10);
                    AppendUtf8(out, cp);
                }
                else { out += raw.substr(i, semi - i + 1); }
                i = semi + 1;
            }
            return out;
        }

        XmlNodePtr ParseElement(XmlNode* parent)
        {
            pos++;                                   // <
            XmlNodePtr node = std::make_shared<XmlNode>();
            node->parent = parent;
            node->Name = ParseName();
            for (;;)
            {
                SkipBlanks();
                if (pos >= src.size()) Fail("unterminated tag <" + node->Name);
                if (StartsHere("/>")) { pos += 2; return node; }
                if (src[pos] == '>') { pos++; break; }
                std::string name = ParseName();
                SkipBlanks();
                if (pos >= src.size() || src[pos] != '=') Fail("'=' expected after attribute " + name);
                pos++;
                SkipBlanks();
                if (pos >= src.size() || (src[pos] != '"' && src[pos] != '\'')) Fail("a quoted value expected for " + name);
                char q = src[pos++];
                size_t end = src.find(q, pos);
                if (end == std::string::npos) Fail("unterminated attribute value");
                std::string value = Decode(src.substr(pos, end - pos));
                pos = end + 1;
                node->Attributes.push_back(std::make_pair(name, value));
            }
            // the content
            std::string text;
            for (;;)
            {
                if (pos >= src.size()) Fail("missing </" + node->Name + ">");
                if (StartsHere("</"))
                {
                    if (!text.empty()) { node->pieces.push_back(std::make_pair(-1, Decode(text))); node->Text += Decode(text); text.clear(); }
                    pos += 2;
                    std::string close = ParseName();
                    if (close != node->Name) Fail("</" + close + "> closes <" + node->Name + ">");
                    SkipBlanks();
                    if (pos >= src.size() || src[pos] != '>') Fail("'>' expected");
                    pos++;
                    return node;
                }
                if (StartsHere("<!--")) { SkipPast("-->"); continue; }
                if (StartsHere("<?")) { SkipPast("?>"); continue; }
                if (StartsHere("<![CDATA["))
                {
                    if (!text.empty()) { node->pieces.push_back(std::make_pair(-1, Decode(text))); node->Text += Decode(text); text.clear(); }
                    size_t start = pos + 9;
                    size_t end = src.find("]]>", start);
                    if (end == std::string::npos) Fail("unterminated CDATA");
                    std::string data = src.substr(start, end - start);
                    node->pieces.push_back(std::make_pair(-1, data));
                    node->Text += data;
                    pos = end + 3;
                    continue;
                }
                if (src[pos] == '<')
                {
                    if (!text.empty()) { node->pieces.push_back(std::make_pair(-1, Decode(text))); node->Text += Decode(text); text.clear(); }
                    XmlNodePtr child = ParseElement(node.get());
                    node->Children.push_back(child);
                    node->pieces.push_back(std::make_pair((int)node->Children.size() - 1, std::string()));
                    continue;
                }
                text += src[pos++];
            }
        }
    };

    // ---- XmlDocument ------------------------------------------------------------
    XmlDocument XmlDocument::Parse(const std::string& text)
    {
        XmlParser p(text);
        XmlDocument d;
        d.DocumentElement = p.ParseDocument();
        return d;
    }

    XmlDocument XmlDocument::Load(const std::string& path)
    {
        std::string text, error;
        Core::FileEncoding enc;
        if (!Core::TextFile::Read(path, text, enc, error)) throw XmlException(error);
        return Parse(text);
    }

    std::vector<XmlNodePtr> XmlDocument::SelectNodes(const std::string& path) const
    {
        if (!DocumentElement) return std::vector<XmlNodePtr>();
        std::string p = path;
        if (!p.empty() && p[0] != '/') p = "/" + p;
        return DocumentElement->SelectNodes(p);
    }

    XmlNodePtr XmlDocument::SelectSingleNode(const std::string& path) const
    {
        std::vector<XmlNodePtr> r = SelectNodes(path);
        return r.empty() ? nullptr : r[0];
    }
}}
