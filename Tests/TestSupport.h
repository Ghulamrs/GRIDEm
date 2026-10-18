// =============================================================================
//  TestSupport.h - what every GRIDEm test file shares: where the source tree and
//  its fixtures are, a fresh temporary folder per test, files in and out, and
//  toolchain.json loaded as the app loads it.
//  GRIDEM_SOURCE_ROOT is set by the test target's build settings
//  (GCC_PREPROCESSOR_DEFINITIONS, Tools/make-xcodeproj.py); the environment
//  variable of the same name wins when set.
// =============================================================================
#pragma once
#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>
#include "Core/Json.h"
#include "Platform/Paths.h"
#include "Settings/ToolchainDefinitions.h"
#include <string>

#ifndef GRIDEM_SOURCE_ROOT
#define GRIDEM_SOURCE_ROOT ""
#endif

// the GRIDEm folder (the one with the Makefile)
static inline std::string GRSourceRoot()
{
    const char* env = getenv("GRIDEM_SOURCE_ROOT");
    if (env != nullptr && env[0] != '\0') return env;
    return GRIDEM_SOURCE_ROOT;
}

static inline std::string GRFixture(const std::string& relative)
{
    return GRSourceRoot() + "/Tests/Fixtures/" + relative;
}

// a new, empty folder under NSTemporaryDirectory()
static inline std::string GRTempDir()
{
    NSString* dir = [NSTemporaryDirectory() stringByAppendingPathComponent:
                     [@"GRIDEmTests-" stringByAppendingString:[[NSUUID UUID] UUIDString]]];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return [dir UTF8String];
}

// copies a fixture folder into a new temporary folder; answers the copy's path
static inline std::string GRCopyFixture(const std::string& relative)
{
    std::string to = GRTempDir() + "/" + GRIDEm::Platform::Paths::FileName(relative);
    NSError* error = nil;
    [[NSFileManager defaultManager] copyItemAtPath:[NSString stringWithUTF8String:GRFixture(relative).c_str()]
                                            toPath:[NSString stringWithUTF8String:to.c_str()]
                                             error:&error];
    return to;
}

static inline std::string GRRead(const std::string& path)
{
    std::string text, error;
    GRIDEm::Platform::Paths::ReadFile(path, text, error);
    return text;
}

static inline void GRWrite(const std::string& path, const std::string& text)
{
    std::string error;
    GRIDEm::Platform::Paths::WriteFile(path, text, error);
}

// GRIDEm's own Resources/toolchain.json, made the current definitions
static inline GRIDEm::Settings::ToolchainDefinitionsPtr GRLoadMacToolchain()
{
    GRIDEm::Settings::ToolchainDefinitionsPtr d =
        GRIDEm::Settings::ToolchainDefinitions::Load(GRSourceRoot() + "/Resources/toolchain.json", std::string());
    GRIDEm::Settings::ToolchainDefinitions::SetCurrent(d);
    return d;
}

// ---- GRAssertEq: XCTAssertEqual for strings as well as numbers ---------------------
// (XCTAssertEqual boxes its values with @encode, which does not suit std::string)
static inline std::string GRDescribe(const std::string& s) { return "\"" + s + "\""; }
static inline std::string GRDescribe(const char* s) { return GRDescribe(std::string(s)); }
static inline std::string GRDescribe(bool v) { return v ? "true" : "false"; }
static inline std::string GRDescribe(int v) { return std::to_string(v); }
static inline std::string GRDescribe(long v) { return std::to_string(v); }
static inline std::string GRDescribe(long long v) { return std::to_string(v); }
static inline std::string GRDescribe(unsigned v) { return std::to_string(v); }
static inline std::string GRDescribe(unsigned long v) { return std::to_string(v); }
static inline std::string GRDescribe(unsigned long long v) { return std::to_string(v); }
static inline std::string GRDescribe(double v) { return std::to_string(v); }

#define GRAssertEq(a, b) \
    XCTAssertTrue((a) == (b), @"%s != %s", GRDescribe(a).c_str(), GRDescribe(b).c_str())

static inline NSString* GRNSString(const std::string& s)
{
    NSString* r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return r != nil ? r : @"";
}
