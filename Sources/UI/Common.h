// =============================================================================
//  Common.h - small helpers every Objective-C++ file of GRIDEm's UI may use.
//  GROr stands in for GNU's "a ?: b", which -pedantic -Werror refuses;
//  GRNS / GRStd turn UTF-8 std::string into NSString and back.
// =============================================================================
#pragma once
#import <Foundation/Foundation.h>
#include <string>

// a when it is not nil, else b
static inline id GROr(id a, id b) { return a != nil ? a : b; }

// UTF-8 std::string <-> NSString
static inline NSString* GRNS(const std::string& s)
{
    NSString* r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return r != nil ? r : @"";
}

static inline std::string GRStd(NSString* s)
{
    if (s == nil) return std::string();
    const char* c = [s UTF8String];
    return c != nullptr ? std::string(c) : std::string();
}
