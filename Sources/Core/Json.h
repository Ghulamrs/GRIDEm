// =============================================================================
//  Json.h - small dependency-free JSON library for GRIDEm
//  Mirrors GRIDEd's Core/Json.h (JsonValue, JsonParser, JsonWriter).
//
//  JsonValue  : one node (null / bool / number / string / array / object),
//               held by JsonPtr (std::shared_ptr) as GRIDEd holds it by handle
//  JsonParser : text  -> JsonValue (accepts // line comments)
//  JsonWriter : JsonValue -> pretty printed text (2-space indent, CRLF, keys in
//               their insertion order) - byte for byte what GRIDEd writes
//  Strings are UTF-8 throughout.
// =============================================================================
#pragma once
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace GRIDEm { namespace Core {

    enum class JsonKind { Null, Bool, Number, String, Array, Object };

    class JsonValue;
    typedef std::shared_ptr<JsonValue> JsonPtr;

    // -------------------------------------------------------------------------
    //  JsonValue
    // -------------------------------------------------------------------------
    class JsonValue
    {
    public:
        JsonValue();

        // ---- factories ------------------------------------------------------
        static JsonPtr MakeNull();
        static JsonPtr MakeObject();
        static JsonPtr MakeArray();
        static JsonPtr FromString(const std::string& s);
        static JsonPtr FromNumber(double d);
        static JsonPtr FromBool(bool b);
        static JsonPtr FromList(const std::vector<std::string>& list);

        // ---- kind -----------------------------------------------------------
        JsonKind Kind() const { return kind; }
        bool IsNull() const   { return kind == JsonKind::Null; }
        bool IsObject() const { return kind == JsonKind::Object; }
        bool IsArray() const  { return kind == JsonKind::Array; }

        // ---- scalar access --------------------------------------------------
        // AsString: false (and out untouched) for null, arrays and objects -
        // GRIDEd's AsString returning nullptr
        bool AsString(std::string& out) const;
        std::string AsStringOr(const std::string& def) const;
        double AsNumber() const;
        bool AsBool() const;

        // ---- array access ---------------------------------------------------
        int Count() const;
        JsonPtr At(int i) const;                 // nullptr when out of range
        void Add(const JsonPtr& v);
        void AddString(const std::string& s) { Add(FromString(s)); }
        std::vector<std::string> ToStringList() const;

        // ---- object access --------------------------------------------------
        std::vector<std::string> KeyNames() const;
        bool Has(const std::string& key) const;
        JsonPtr Get(const std::string& key) const;   // nullptr when absent
        void SetValue(const std::string& key, const JsonPtr& v);
        void Remove(const std::string& key);
        // distinct names on purpose, as in GRIDEd: an overloaded Set("k","text")
        // would bind the string literal to bool
        void SetString(const std::string& key, const std::string& s) { SetValue(key, FromString(s)); }
        void SetNumber(const std::string& key, double d)            { SetValue(key, FromNumber(d)); }
        void SetBool(const std::string& key, bool b)                { SetValue(key, FromBool(b)); }
        void SetList(const std::string& key, const std::vector<std::string>& l) { SetValue(key, FromList(l)); }

        // ---- typed getters with defaults ------------------------------------
        std::string GetString(const std::string& key, const std::string& def) const;
        double GetNumber(const std::string& key, double def) const;
        int GetInt(const std::string& key, int def) const;
        bool GetBool(const std::string& key, bool def) const;
        std::vector<std::string> GetStringList(const std::string& key) const;
        JsonPtr GetObject(const std::string& key) const;   // nullptr unless an object

        // a deep copy (GRIDEd clones settings before an Options dialog edits them)
        JsonPtr Clone() const;

    private:
        friend class JsonWriter;
        JsonKind kind;
        std::string str;
        double num;
        bool flag;
        std::vector<JsonPtr> items;
        std::map<std::string, JsonPtr> map;
        std::vector<std::string> order;
    };

    // -------------------------------------------------------------------------
    //  JsonException - "JSON error at line L, column C: message"
    // -------------------------------------------------------------------------
    class JsonException : public std::runtime_error
    {
    public:
        JsonException(const std::string& msg, int line, int col);
    };

    // -------------------------------------------------------------------------
    //  JsonParser
    // -------------------------------------------------------------------------
    class JsonParser
    {
    public:
        static JsonPtr Parse(const std::string& text);        // throws JsonException
        static JsonPtr ParseFile(const std::string& path);    // throws JsonException or std::runtime_error

    private:
        explicit JsonParser(const std::string& s);
        void Fail(const std::string& msg) const;
        void SkipWs();
        JsonPtr ParseValue();
        void Expect(const char* word);
        JsonPtr ParseObject();
        JsonPtr ParseArray();
        std::string ParseString();
        JsonPtr ParseNumber();

        const std::string& src;
        size_t pos;
    };

    // -------------------------------------------------------------------------
    //  JsonWriter
    // -------------------------------------------------------------------------
    class JsonWriter
    {
    public:
        static std::string Write(const JsonPtr& v);
        // writes a temp file first and swaps it in, so a crash or a full disk
        // never leaves a half-written settings / project / workspace file.
        // Throws std::runtime_error when the file cannot be written.
        static void WriteFile(const std::string& path, const JsonPtr& v);
        static std::string Quote(const std::string& s);
        // a number as .NET's "R" format writes it: the shortest text that reads back
        static std::string FormatNumber(double d);

    private:
        static void Indent(std::string& sb, int level);
        static bool IsScalarArray(const JsonValue& v);
        static void WriteValue(std::string& sb, const JsonPtr& v, int level);
    };
}}
