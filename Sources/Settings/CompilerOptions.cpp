// =============================================================================
//  CompilerOptions.cpp - see CompilerOptions.h
// =============================================================================
#include "CompilerOptions.h"
#include "../Core/StringUtil.h"

namespace GRIDEm { namespace Settings {

    using namespace GRIDEm::Core;

    std::vector<std::string> CompilerOptionStore::Configurations()
    {
        std::vector<std::string> r;
        r.push_back("Debug");
        r.push_back("Release");
        return r;
    }

    std::string CompilerOptionStore::Canon(const std::string& config)
    {
        return StringUtil::EqualsIgnoreCase(StringUtil::Trim(config), "Release") ? "Release" : "Debug";
    }

    std::string CompilerOptionStore::RideName(const std::string& config)
    {
        return StringUtil::ToLower(Canon(config));
    }

    std::string CompilerOptionStore::DefaultValue(const std::string& config, const std::string& id)
    {
        CompilerOptionDefinitionPtr d = ToolchainDefinitions::Current()->CompilerOption(id);
        return d ? d->DefaultFor(Canon(config)) : std::string();
    }

    CompilerOptionStore::Values* CompilerOptionStore::Find(const std::string& canonConfig)
    {
        for (std::pair<std::string, Values>& v : values) if (v.first == canonConfig) return &v.second;
        return nullptr;
    }

    const CompilerOptionStore::Values* CompilerOptionStore::Find(const std::string& canonConfig) const
    {
        for (const std::pair<std::string, Values>& v : values) if (v.first == canonConfig) return &v.second;
        return nullptr;
    }

    std::string CompilerOptionStore::Value(const std::string& config, const std::string& id) const
    {
        const Values* one = Find(Canon(config));
        if (one != nullptr)
            for (const std::pair<std::string, std::string>& kv : *one) if (kv.first == id) return kv.second;
        return DefaultValue(config, id);
    }

    void CompilerOptionStore::Unset(const std::string& config, const std::string& id)
    {
        Values* one = Find(Canon(config));
        if (one == nullptr) return;
        for (size_t i = 0; i < one->size(); i++)
            if ((*one)[i].first == id) { one->erase(one->begin() + (long)i); return; }
    }

    void CompilerOptionStore::Set(const std::string& config, const std::string& id, const std::string& value)
    {
        std::string c = Canon(config);
        Values* one = Find(c);
        if (one == nullptr)
        {
            values.push_back(std::make_pair(c, Values()));
            one = &values.back().second;
        }
        if (value == DefaultValue(config, id)) { Unset(config, id); return; }
        for (std::pair<std::string, std::string>& kv : *one)
            if (kv.first == id) { kv.second = value; return; }
        one->push_back(std::make_pair(id, value));
    }

    void CompilerOptionStore::Reset(const std::string& config)
    {
        Values* one = Find(Canon(config));
        if (one != nullptr) one->clear();
    }

    bool CompilerOptionStore::Empty() const
    {
        for (const std::pair<std::string, Values>& v : values) if (!v.second.empty()) return false;
        return true;
    }

    JsonPtr CompilerOptionStore::ToJson() const
    {
        JsonPtr root = JsonValue::MakeObject();
        for (const std::string& c : Configurations())
        {
            const Values* one = Find(c);
            if (one == nullptr || one->empty()) continue;
            JsonPtr o = JsonValue::MakeObject();
            for (const std::pair<std::string, std::string>& kv : *one) o->SetString(kv.first, kv.second);
            root->SetValue(RideName(c), o);
        }
        return root;
    }

    void CompilerOptionStore::FromJson(const JsonPtr& json)
    {
        values.clear();
        if (!json || !json->IsObject()) return;
        for (const std::string& key : json->KeyNames())
        {
            JsonPtr one = json->GetObject(key);
            if (!one) continue;
            std::string c = Canon(key);
            for (const std::string& id : one->KeyNames())
                if (ToolchainDefinitions::Current()->CompilerOption(id)) Set(c, id, one->GetString(id, ""));
        }
    }

    CompilerOptionStorePtr CompilerOptionStore::Clone() const
    {
        CompilerOptionStorePtr c = std::make_shared<CompilerOptionStore>();
        c->FromJson(ToJson());
        return c;
    }

    void CompilerOptionStore::CopyFrom(const CompilerOptionStorePtr& other)
    {
        values.clear();
        if (other) FromJson(other->ToJson());
    }

    std::vector<std::string> CompilerOptionStore::Entries(const std::string& list)
    {
        std::vector<std::string> r;
        for (const std::string& s : StringUtil::Split(list, ';'))
        {
            std::string t = s;
            size_t b = 0, e = t.size();
            while (b < e && (t[b] == ' ' || t[b] == '\t')) b++;
            while (e > b && (t[e - 1] == ' ' || t[e - 1] == '\t')) e--;
            t = t.substr(b, e - b);
            if (!t.empty()) r.push_back(t);
        }
        return r;
    }
}}
