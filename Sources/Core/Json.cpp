// =============================================================================
//  Json.cpp - see Json.h
// =============================================================================
#include "Json.h"
#include "../Platform/Paths.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace GRIDEm { namespace Core {

    // =========================================================================
    //  JsonValue
    // =========================================================================
    JsonValue::JsonValue() : kind(JsonKind::Null), num(0.0), flag(false) {}

    JsonPtr JsonValue::MakeNull() { return std::make_shared<JsonValue>(); }

    JsonPtr JsonValue::MakeObject()
    {
        JsonPtr v = std::make_shared<JsonValue>();
        v->kind = JsonKind::Object;
        return v;
    }

    JsonPtr JsonValue::MakeArray()
    {
        JsonPtr v = std::make_shared<JsonValue>();
        v->kind = JsonKind::Array;
        return v;
    }

    JsonPtr JsonValue::FromString(const std::string& s)
    {
        JsonPtr v = std::make_shared<JsonValue>();
        v->kind = JsonKind::String;
        v->str = s;
        return v;
    }

    JsonPtr JsonValue::FromNumber(double d)
    {
        JsonPtr v = std::make_shared<JsonValue>();
        v->kind = JsonKind::Number;
        v->num = d;
        return v;
    }

    JsonPtr JsonValue::FromBool(bool b)
    {
        JsonPtr v = std::make_shared<JsonValue>();
        v->kind = JsonKind::Bool;
        v->flag = b;
        return v;
    }

    JsonPtr JsonValue::FromList(const std::vector<std::string>& list)
    {
        JsonPtr a = MakeArray();
        for (const std::string& s : list) a->Add(FromString(s));
        return a;
    }

    bool JsonValue::AsString(std::string& out) const
    {
        switch (kind)
        {
        case JsonKind::String: out = str; return true;
        case JsonKind::Number: out = JsonWriter::FormatNumber(num); return true;
        case JsonKind::Bool:   out = flag ? "true" : "false"; return true;
        default:               return false;
        }
    }

    std::string JsonValue::AsStringOr(const std::string& def) const
    {
        std::string s;
        if (AsString(s)) return s;
        return def;
    }

    static bool EqualsIgnoreCase(const std::string& a, const char* b)
    {
        size_t n = std::strlen(b);
        if (a.size() != n) return false;
        for (size_t i = 0; i < n; i++)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }

    // Double::TryParse with NumberStyles::Float: optional surrounding blanks,
    // leading sign, digits, point, exponent - and nothing else
    static bool TryParseDouble(const std::string& s, double& out)
    {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
        if (b == e) return false;
        std::string t = s.substr(b, e - b);
        for (char c : t)
        {
            bool ok = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
            if (!ok) return false;
        }
        char* end = nullptr;
        double d = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0') return false;
        out = d;
        return true;
    }

    double JsonValue::AsNumber() const
    {
        if (kind == JsonKind::Number) return num;
        if (kind == JsonKind::String)
        {
            double d = 0.0;
            if (TryParseDouble(str, d)) return d;
        }
        if (kind == JsonKind::Bool) return flag ? 1.0 : 0.0;
        return 0.0;
    }

    bool JsonValue::AsBool() const
    {
        if (kind == JsonKind::Bool) return flag;
        if (kind == JsonKind::Number) return num != 0.0;
        if (kind == JsonKind::String) return EqualsIgnoreCase(str, "true");
        return false;
    }

    int JsonValue::Count() const
    {
        if (kind == JsonKind::Array)  return (int)items.size();
        if (kind == JsonKind::Object) return (int)order.size();
        return 0;
    }

    JsonPtr JsonValue::At(int i) const
    {
        if (kind == JsonKind::Array && i >= 0 && i < (int)items.size()) return items[(size_t)i];
        return nullptr;
    }

    void JsonValue::Add(const JsonPtr& v)
    {
        if (kind != JsonKind::Array) return;
        items.push_back(v ? v : MakeNull());
    }

    std::vector<std::string> JsonValue::ToStringList() const
    {
        std::vector<std::string> r;
        if (kind == JsonKind::Array)
            for (const JsonPtr& v : items)
            {
                std::string s;
                if (v->AsString(s)) r.push_back(s);
            }
        return r;
    }

    std::vector<std::string> JsonValue::KeyNames() const
    {
        if (kind == JsonKind::Object) return order;
        return std::vector<std::string>();
    }

    bool JsonValue::Has(const std::string& key) const
    {
        return kind == JsonKind::Object && map.find(key) != map.end();
    }

    JsonPtr JsonValue::Get(const std::string& key) const
    {
        if (kind != JsonKind::Object) return nullptr;
        std::map<std::string, JsonPtr>::const_iterator it = map.find(key);
        if (it == map.end()) return nullptr;
        return it->second;
    }

    void JsonValue::SetValue(const std::string& key, const JsonPtr& v)
    {
        if (kind != JsonKind::Object) return;
        if (map.find(key) == map.end()) order.push_back(key);
        map[key] = v ? v : MakeNull();
    }

    void JsonValue::Remove(const std::string& key)
    {
        if (kind != JsonKind::Object) return;
        std::map<std::string, JsonPtr>::iterator it = map.find(key);
        if (it == map.end()) return;
        map.erase(it);
        for (size_t i = 0; i < order.size(); i++)
            if (order[i] == key) { order.erase(order.begin() + (long)i); break; }
    }

    std::string JsonValue::GetString(const std::string& key, const std::string& def) const
    {
        JsonPtr v = Get(key);
        if (!v || v->kind == JsonKind::Null) return def;
        std::string s;
        if (!v->AsString(s)) return def;
        return s;
    }

    double JsonValue::GetNumber(const std::string& key, double def) const
    {
        JsonPtr v = Get(key);
        if (!v || v->kind == JsonKind::Null) return def;
        return v->AsNumber();
    }

    // Math::Round is banker's rounding (to even), as in GRIDEd
    int JsonValue::GetInt(const std::string& key, int def) const
    {
        double d = GetNumber(key, (double)def);
        return (int)std::nearbyint(d);
    }

    bool JsonValue::GetBool(const std::string& key, bool def) const
    {
        JsonPtr v = Get(key);
        if (!v || v->kind == JsonKind::Null) return def;
        return v->AsBool();
    }

    std::vector<std::string> JsonValue::GetStringList(const std::string& key) const
    {
        JsonPtr v = Get(key);
        if (!v) return std::vector<std::string>();
        return v->ToStringList();
    }

    JsonPtr JsonValue::GetObject(const std::string& key) const
    {
        JsonPtr v = Get(key);
        if (v && v->IsObject()) return v;
        return nullptr;
    }

    JsonPtr JsonValue::Clone() const
    {
        JsonPtr c = std::make_shared<JsonValue>();
        c->kind = kind;
        c->str = str;
        c->num = num;
        c->flag = flag;
        for (const JsonPtr& i : items) c->items.push_back(i->Clone());
        c->order = order;
        for (std::map<std::string, JsonPtr>::const_iterator it = map.begin(); it != map.end(); ++it)
            c->map[it->first] = it->second->Clone();
        return c;
    }

    // =========================================================================
    //  JsonException
    // =========================================================================
    static std::string ErrorText(const std::string& msg, int line, int col)
    {
        return "JSON error at line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + msg;
    }

    JsonException::JsonException(const std::string& msg, int line, int col)
        : std::runtime_error(ErrorText(msg, line, col)) {}

    // =========================================================================
    //  JsonParser
    // =========================================================================
    JsonParser::JsonParser(const std::string& s) : src(s), pos(0) {}

    JsonPtr JsonParser::Parse(const std::string& text)
    {
        JsonParser p(text);
        // a UTF-8 byte order mark is not part of the document
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
            && (unsigned char)text[2] == 0xBF)
            p.pos = 3;
        p.SkipWs();
        JsonPtr v = p.ParseValue();
        p.SkipWs();
        if (p.pos < p.src.size()) p.Fail("unexpected text after the end of the document");
        return v;
    }

    JsonPtr JsonParser::ParseFile(const std::string& path)
    {
        std::string text;
        std::string error;
        if (!Platform::Paths::ReadFile(path, text, error)) throw std::runtime_error(error);
        return Parse(text);
    }

    // line and column count characters, not bytes, as GRIDEd's UTF-16 index does
    void JsonParser::Fail(const std::string& msg) const
    {
        int line = 1, col = 1;
        for (size_t i = 0; i < pos && i < src.size(); i++)
        {
            unsigned char c = (unsigned char)src[i];
            if ((c & 0xC0) == 0x80) continue;       // a UTF-8 continuation byte
            if (c == '\n') { line++; col = 1; } else col++;
        }
        throw JsonException(msg, line, col);
    }

    void JsonParser::SkipWs()
    {
        while (pos < src.size())
        {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { pos++; continue; }
            // tolerate // line comments - handy in hand-edited settings
            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/')
            {
                while (pos < src.size() && src[pos] != '\n') pos++;
                continue;
            }
            break;
        }
    }

    JsonPtr JsonParser::ParseValue()
    {
        if (pos >= src.size()) Fail("unexpected end of document");
        char c = src[pos];
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return JsonValue::FromString(ParseString());
        if (c == 't') { Expect("true");  return JsonValue::FromBool(true); }
        if (c == 'f') { Expect("false"); return JsonValue::FromBool(false); }
        if (c == 'n') { Expect("null");  return JsonValue::MakeNull(); }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        Fail(std::string("unexpected character '") + c + "'");
        return nullptr;
    }

    void JsonParser::Expect(const char* word)
    {
        size_t n = std::strlen(word);
        if (src.compare(pos, n, word) != 0) Fail(std::string("expected '") + word + "'");
        pos += n;
    }

    JsonPtr JsonParser::ParseObject()
    {
        JsonPtr obj = JsonValue::MakeObject();
        pos++;                                  // {
        SkipWs();
        if (pos < src.size() && src[pos] == '}') { pos++; return obj; }
        while (true)
        {
            SkipWs();
            if (pos >= src.size() || src[pos] != '"') Fail("expected a quoted key");
            std::string key = ParseString();
            SkipWs();
            if (pos >= src.size() || src[pos] != ':') Fail("expected ':'");
            pos++;
            SkipWs();
            obj->SetValue(key, ParseValue());
            SkipWs();
            if (pos >= src.size()) Fail("unterminated object");
            if (src[pos] == ',')
            {
                pos++;
                SkipWs();
                if (pos < src.size() && src[pos] == '}') { pos++; return obj; }
                continue;
            }
            if (src[pos] == '}') { pos++; return obj; }
            Fail("expected ',' or '}'");
        }
    }

    JsonPtr JsonParser::ParseArray()
    {
        JsonPtr arr = JsonValue::MakeArray();
        pos++;                                  // [
        SkipWs();
        if (pos < src.size() && src[pos] == ']') { pos++; return arr; }
        while (true)
        {
            SkipWs();
            arr->Add(ParseValue());
            SkipWs();
            if (pos >= src.size()) Fail("unterminated array");
            if (src[pos] == ',')
            {
                pos++;
                SkipWs();
                if (pos < src.size() && src[pos] == ']') { pos++; return arr; }
                continue;
            }
            if (src[pos] == ']') { pos++; return arr; }
            Fail("expected ',' or ']'");
        }
    }

    static void AppendUtf8(std::string& out, unsigned int cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
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

    static bool ReadHex4(const std::string& s, size_t at, unsigned int& out)
    {
        if (at + 4 > s.size()) return false;
        unsigned int v = 0;
        for (size_t i = at; i < at + 4; i++)
        {
            char c = s[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned int)(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    std::string JsonParser::ParseString()
    {
        std::string sb;
        pos++;                                  // opening quote
        while (pos < src.size())
        {
            char c = src[pos++];
            if (c == '"') return sb;
            if (c != '\\') { sb += c; continue; }
            if (pos >= src.size()) break;
            char e = src[pos++];
            switch (e)
            {
            case '"':  sb += '"';  break;
            case '\\': sb += '\\'; break;
            case '/':  sb += '/';  break;
            case 'b':  sb += '\b'; break;
            case 'f':  sb += '\f'; break;
            case 'n':  sb += '\n'; break;
            case 'r':  sb += '\r'; break;
            case 't':  sb += '\t'; break;
            case 'u':
            {
                unsigned int code = 0;
                if (!ReadHex4(src, pos, code)) Fail("bad \\u escape");
                pos += 4;
                // a surrogate pair written as two escapes is one character
                if (code >= 0xD800 && code <= 0xDBFF && pos + 6 <= src.size()
                    && src[pos] == '\\' && src[pos + 1] == 'u')
                {
                    unsigned int low = 0;
                    if (ReadHex4(src, pos + 2, low) && low >= 0xDC00 && low <= 0xDFFF)
                    {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        pos += 6;
                    }
                }
                AppendUtf8(sb, code);
                break;
            }
            default: Fail("bad escape sequence");
            }
        }
        Fail("unterminated string");
        return std::string();
    }

    JsonPtr JsonParser::ParseNumber()
    {
        size_t start = pos;
        if (src[pos] == '-') pos++;
        while (pos < src.size())
        {
            char c = src[pos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') pos++;
            else break;
        }
        double d = 0.0;
        if (!TryParseDouble(src.substr(start, pos - start), d)) Fail("bad number");
        return JsonValue::FromNumber(d);
    }

    // =========================================================================
    //  JsonWriter
    // =========================================================================
    std::string JsonWriter::Write(const JsonPtr& v)
    {
        std::string sb;
        WriteValue(sb, v, 0);
        sb += "\r\n";
        return sb;
    }

    void JsonWriter::WriteFile(const std::string& path, const JsonPtr& v)
    {
        std::string error;
        if (!Platform::Paths::WriteFileAtomically(path, Write(v), error)) throw std::runtime_error(error);
    }

    std::string JsonWriter::Quote(const std::string& s)
    {
        std::string sb = "\"";
        for (char ch : s)
        {
            unsigned char c = (unsigned char)ch;
            switch (c)
            {
            case '"':  sb += "\\\""; break;
            case '\\': sb += "\\\\"; break;
            case '\n': sb += "\\n";  break;
            case '\r': sb += "\\r";  break;
            case '\t': sb += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned int)c);
                    sb += buf;
                }
                else sb += ch;
            }
        }
        sb += '"';
        return sb;
    }

    // .NET Framework's "R": G15 when it reads back exactly, else G17.
    // C's %G and .NET's G agree on when to use the exponent and on its shape
    // (1E+20, 1E-05).
    std::string JsonWriter::FormatNumber(double d)
    {
        if (std::isnan(d)) return "NaN";
        if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.15G", d);
        if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof buf, "%.17G", d);
        std::string s = buf;
        if (s == "-0") s = "0";
        return s;
    }

    void JsonWriter::Indent(std::string& sb, int level)
    {
        sb.append((size_t)(level * 2), ' ');
    }

    bool JsonWriter::IsScalarArray(const JsonValue& v)
    {
        for (const JsonPtr& i : v.items)
            if (i->kind == JsonKind::Object || i->kind == JsonKind::Array) return false;
        return true;
    }

    // the width of a line in characters, as GRIDEd's StringBuilder::Length counts them
    static size_t CharacterCount(const std::string& s)
    {
        size_t n = 0;
        for (char c : s)
            if (((unsigned char)c & 0xC0) != 0x80) n++;
        return n;
    }

    void JsonWriter::WriteValue(std::string& sb, const JsonPtr& v, int level)
    {
        if (!v) { sb += "null"; return; }
        switch (v->kind)
        {
        case JsonKind::Null:   sb += "null"; break;
        case JsonKind::Bool:   sb += v->flag ? "true" : "false"; break;
        case JsonKind::Number: sb += FormatNumber(v->num); break;
        case JsonKind::String: sb += Quote(v->str); break;
        case JsonKind::Array:
        {
            if (v->items.empty()) { sb += "[]"; break; }
            if (IsScalarArray(*v))
            {
                // short scalar arrays stay on one line: ["a", "b"]
                std::string line = "[";
                for (size_t i = 0; i < v->items.size(); i++)
                {
                    if (i > 0) line += ", ";
                    WriteValue(line, v->items[i], 0);
                }
                line += ']';
                if (CharacterCount(line) <= 90) { sb += line; break; }
            }
            sb += "[\r\n";
            for (size_t i = 0; i < v->items.size(); i++)
            {
                Indent(sb, level + 1);
                WriteValue(sb, v->items[i], level + 1);
                if (i < v->items.size() - 1) sb += ',';
                sb += "\r\n";
            }
            Indent(sb, level);
            sb += ']';
            break;
        }
        case JsonKind::Object:
        {
            if (v->order.empty()) { sb += "{}"; break; }
            sb += "{\r\n";
            for (size_t i = 0; i < v->order.size(); i++)
            {
                const std::string& k = v->order[i];
                Indent(sb, level + 1);
                sb += Quote(k);
                sb += ": ";
                WriteValue(sb, v->map.find(k)->second, level + 1);
                if (i < v->order.size() - 1) sb += ',';
                sb += "\r\n";
            }
            Indent(sb, level);
            sb += '}';
            break;
        }
        }
    }

    // ---- RIDE's layout ----------------------------------------------------------------
    std::string JsonWriter::WriteRide(const JsonPtr& v)
    {
        std::string sb;
        WriteRideValue(sb, v, 0);
        return sb + "\n";
    }

    void JsonWriter::WriteRideFile(const std::string& path, const JsonPtr& v)
    {
        std::string error;
        if (!Platform::Paths::WriteFileAtomically(path, WriteRide(v), error)) throw std::runtime_error(error);
    }

    void JsonWriter::WriteRideValue(std::string& sb, const JsonPtr& v, int level)
    {
        if (!v) { sb += "null"; return; }
        switch (v->kind)
        {
        case JsonKind::Null:   sb += "null"; break;
        case JsonKind::Bool:   sb += v->flag ? "true" : "false"; break;
        case JsonKind::String: sb += Quote(v->str); break;
        case JsonKind::Number:
        {
            char buf[32];
            if (v->num == (double)(long)v->num) std::snprintf(buf, sizeof buf, "%ld", (long)v->num);
            else std::snprintf(buf, sizeof buf, "%g", v->num);
            sb += buf;
            break;
        }
        case JsonKind::Array:
        {
            if (v->items.empty()) { sb += "[]"; break; }
            sb += "[\n";
            for (size_t i = 0; i < v->items.size(); i++)
            {
                Indent(sb, level + 1);
                WriteRideValue(sb, v->items[i], level + 1);
                if (i < v->items.size() - 1) sb += ',';
                sb += "\n";
            }
            Indent(sb, level);
            sb += ']';
            break;
        }
        case JsonKind::Object:
        {
            if (v->order.empty()) { sb += "{}"; break; }
            sb += "{\n";
            for (size_t i = 0; i < v->order.size(); i++)
            {
                const std::string& k = v->order[i];
                Indent(sb, level + 1);
                sb += Quote(k);
                sb += ": ";
                WriteRideValue(sb, v->map.find(k)->second, level + 1);
                if (i < v->order.size() - 1) sb += ',';
                sb += "\n";
            }
            Indent(sb, level);
            sb += '}';
            break;
        }
        }
    }
}}
