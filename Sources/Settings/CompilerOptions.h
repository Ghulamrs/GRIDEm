// =============================================================================
//  CompilerOptions.h - RIDE's Compiler Options store (options.cpp's Store): one value per
//  option id and configuration, kept only where it differs from the definition's default,
//  so an untouched project's file is unchanged. The table itself is toolchain.json's
//  compilerOptions section; what a store becomes on a command line is
//  RideToolchain::OptionFlags. A project carries its store in the .pro under "options"
//  ({ "debug": {...}, "release": {...} }); the settings carry one under "compilerOptions".
//  (GRIDEd's Settings/CompilerOptions.h)
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "ToolchainDefinitions.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Settings {

    class CompilerOptionStore;
    typedef std::shared_ptr<CompilerOptionStore> CompilerOptionStorePtr;

    class CompilerOptionStore
    {
    public:
        static std::vector<std::string> Configurations();       // "Debug", "Release"
        // RIDE's two configurations, however a caller spells them
        static std::string Canon(const std::string& config);
        static std::string RideName(const std::string& config);  // "debug" / "release"
        // the definition's default for a configuration; "" for an option the file does not know
        static std::string DefaultValue(const std::string& config, const std::string& id);

        std::string Value(const std::string& config, const std::string& id) const;
        // a value equal to the default is not kept, as RIDE's set
        void Set(const std::string& config, const std::string& id, const std::string& value);
        void Unset(const std::string& config, const std::string& id);   // GRIDEd's Set(..., nullptr)
        void Reset(const std::string& config);
        bool Empty() const;

        // only what differs from the defaults, under RIDE's lowercase configuration names
        Core::JsonPtr ToJson() const;
        // an id the definitions do not know is left out; either spelling of the configuration reads
        void FromJson(const Core::JsonPtr& json);
        CompilerOptionStorePtr Clone() const;
        void CopyFrom(const CompilerOptionStorePtr& other);

        // the entries of a text option: ';' between them, blanks trimmed, empty ones dropped
        static std::vector<std::string> Entries(const std::string& list);

    private:
        // per configuration, the values in the order they were set (the order GRIDEd writes them)
        typedef std::vector<std::pair<std::string, std::string> > Values;
        std::vector<std::pair<std::string, Values> > values;
        Values* Find(const std::string& canonConfig);
        const Values* Find(const std::string& canonConfig) const;
    };
}}
