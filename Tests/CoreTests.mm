// =============================================================================
//  CoreTests.mm - Core and the command-line reader (reference section 7.1):
//  JSON as GRIDEd reads and writes it, paths, text files, strings.
// =============================================================================
#import "TestSupport.h"
#include "Core/Json.h"
#include "Core/LanguageTarget.h"
#include "Core/PathUtil.h"
#include "Core/StringUtil.h"
#include "Core/TextFile.h"
#include "Platform/CommandLine.h"

using namespace GRIDEm;
using namespace GRIDEm::Core;

@interface CoreTests : XCTestCase
@end

@implementation CoreTests

// ---- JSON -------------------------------------------------------------------

- (void)testJsonWritesCrlfKeyOrderAndShortArraysOnOneLine
{
    JsonPtr o = JsonParser::Parse("{\"b\":1,\"a\":[1,\"x\",true,null],\"o\":{}}");
    std::string expected = "{\r\n  \"b\": 1,\r\n  \"a\": [1, \"x\", true, null],\r\n  \"o\": {}\r\n}\r\n";
    GRAssertEq(JsonWriter::Write(o), expected);
}

- (void)testJsonLongScalarArrayBreaksLines
{
    JsonPtr a = JsonValue::MakeArray();
    for (int i = 0; i < 12; i++) a->AddString("element-" + std::to_string(i));
    std::string text = JsonWriter::Write(a);
    XCTAssertTrue(text.find("[\r\n  \"element-0\",\r\n") == 0);
    // a short one stays on one line, up to 90 characters
    JsonPtr b = JsonValue::MakeArray();
    b->AddString(std::string(86, 'x'));
    GRAssertEq(JsonWriter::Write(b), "[\"" + std::string(86, 'x') + "\"]\r\n");
    JsonPtr c = JsonValue::MakeArray();
    c->AddString(std::string(87, 'x'));
    GRAssertEq(JsonWriter::Write(c).substr(0, 3), std::string("[\r\n"));
}

- (void)testJsonNumbersAsDotNetRoundTrip
{
    GRAssertEq(JsonWriter::FormatNumber(1), "1");
    GRAssertEq(JsonWriter::FormatNumber(2.5), "2.5");
    GRAssertEq(JsonWriter::FormatNumber(0.1), "0.1");
    GRAssertEq(JsonWriter::FormatNumber(-3), "-3");
    GRAssertEq(JsonWriter::FormatNumber(1e20), "1E+20");
    GRAssertEq(JsonWriter::FormatNumber(1e-5), "1E-05");
    GRAssertEq(JsonWriter::FormatNumber(1.0 / 3), "0.33333333333333331");
}

- (void)testJsonEscapesAndSurrogatePairs
{
    JsonPtr v = JsonParser::Parse("\"\\ud83d\\ude00\\u00e9\\n\\t\\\"\"");
    std::string s;
    XCTAssertTrue(v->AsString(s));
    GRAssertEq(s, std::string("\xF0\x9F\x98\x80\xC3\xA9\n\t\""));
    GRAssertEq(JsonWriter::Quote(std::string("a\x01" "b")), std::string("\"a\\u0001b\""));
    GRAssertEq(JsonWriter::Quote("q\"\\"), std::string("\"q\\\"\\\\\""));
}

- (void)testJsonLineCommentsAndTrailingCommas
{
    JsonPtr o = JsonParser::Parse("// header\n{\n  \"a\": 1, // after\n  \"b\": [1, 2,],\n}\n");
    GRAssertEq(o->GetInt("a", 0), 1);
    GRAssertEq(o->Get("b")->Count(), 2);
}

- (void)testJsonErrorsNameLineAndColumn
{
    try
    {
        JsonParser::Parse("{\n  \"a\": ?\n}");
        XCTFail(@"no exception");
    }
    catch (const JsonException& ex)
    {
        XCTAssertTrue(std::string(ex.what()).find("line 2, column 8") != std::string::npos, @"%s", ex.what());
    }
}

- (void)testJsonTypedGettersAndRemove
{
    JsonPtr o = JsonValue::MakeObject();
    o->SetString("s", "x");
    o->SetNumber("n", 2.5);
    o->SetBool("b", true);
    o->SetList("l", std::vector<std::string>{ "p", "q" });
    GRAssertEq(o->GetString("s", ""), "x");
    GRAssertEq(o->GetString("missing", "def"), "def");
    GRAssertEq(o->GetNumber("n", 0), 2.5);
    XCTAssertTrue(o->GetBool("b", false));
    GRAssertEq(o->GetStringList("l").size(), (size_t)2);
    o->Remove("s");
    XCTAssertFalse(o->Has("s"));
    GRAssertEq(o->KeyNames().front(), "n");
}

- (void)testJsonWriteReadWriteIsStable
{
    std::string text = "{\r\n  \"name\": \"caf\xC3\xA9\",\r\n  \"list\": [\"a\", \"b\"],\r\n  \"nested\": {\r\n    \"x\": 1.5\r\n  }\r\n}\r\n";
    GRAssertEq(JsonWriter::Write(JsonParser::Parse(text)), text);
}

- (void)testJsonFileWrittenAtomically
{
    std::string path = GRTempDir() + "/sub/settings.json";
    JsonPtr o = JsonValue::MakeObject();
    o->SetString("k", "v");
    JsonWriter::WriteFile(path, o);
    GRAssertEq(GRRead(path), "{\r\n  \"k\": \"v\"\r\n}\r\n");
    XCTAssertFalse(Platform::Paths::Exists(path + ".tmp"));
}

// ---- paths ------------------------------------------------------------------

- (void)testPathRelativeAndAbsolute
{
    GRAssertEq(PathUtil::MakeRelative("/a/b", "/a/c/d.c"), "../c/d.c");
    GRAssertEq(PathUtil::MakeRelative("/a/B", "/a/b/x/y.c"), "x/y.c");      // case ignored
    GRAssertEq(PathUtil::MakeRelative("/a/b", "/a/b"), ".");
    GRAssertEq(PathUtil::MakeAbsolute("/a/b", "src\\x.c"), "/a/b/src/x.c"); // written on Windows
    GRAssertEq(PathUtil::MakeAbsolute("/a/b", "../c/./d.c"), "/a/c/d.c");
    GRAssertEq(PathUtil::MakeAbsolute("/a/b", "/abs/e.c"), "/abs/e.c");
}

- (void)testPathComparisons
{
    XCTAssertTrue(PathUtil::SamePath("/a/B/c.C", "/a/b/c.c"));
    XCTAssertTrue(PathUtil::IsStrictlyUnder("/a/b", "/a/b/c"));
    XCTAssertFalse(PathUtil::IsStrictlyUnder("/a/b", "/a/b"));
    XCTAssertFalse(PathUtil::IsStrictlyUnder("/a/b", "/a/bc/d"));
    XCTAssertTrue(PathUtil::HasExtension("x/Main.C", ".c"));
    GRAssertEq(PathUtil::Quote("a b"), "\"a b\"");
    GRAssertEq(PathUtil::Quote(""), "\"\"");
}

- (void)testPathsNames
{
    using Platform::Paths;
    GRAssertEq(Paths::FileName("/x/y/z.tar.gz"), "z.tar.gz");
    GRAssertEq(Paths::Extension("/x/y/z.tar.gz"), ".gz");
    GRAssertEq(Paths::FileNameWithoutExtension("/x/y/z.c"), "z");
    GRAssertEq(Paths::ChangeExtension("/x/y/z.c", ".obj"), "/x/y/z.obj");
    GRAssertEq(Paths::DirectoryName("/x/y/z.c"), "/x/y");
    GRAssertEq(Paths::FullPath("/x/./y/../z"), "/x/z");
}

// ---- languages and targets ---------------------------------------------------

- (void)testLanguageAndTargetNamesRoundTrip
{
    for (Language l : { Language::C90, Language::Cpp11, Language::Shalimar, Language::Assembly,
                        Language::Header, Language::LinkerCmd })
        XCTAssertTrue(LanguageInfo::Parse(LanguageInfo::Name(l)) == l);
    for (Target t : TargetInfo::All())
        XCTAssertTrue(TargetInfo::Parse(TargetInfo::Name(t)) == t);
    GRAssertEq(ConfigName::Make("Debug", Target::C6747), "Debug|TMS320C6747");
    GRAssertEq(ConfigName::TargetPart("Debug|macOS"), "macOS");
}

// ---- text files -------------------------------------------------------------

- (void)testTextFileUtf8AndBom
{
    FileEncoding e;
    GRAssertEq(TextFile::Decode("caf\xC3\xA9\r\n", e), "caf\xC3\xA9\r\n");
    XCTAssertTrue(e == FileEncoding::Utf8);
    GRAssertEq(TextFile::Decode("\xEF\xBB\xBFx", e), "x");
    XCTAssertTrue(e == FileEncoding::Utf8Bom);
    GRAssertEq(TextFile::Encode("x", e), "\xEF\xBB\xBFx");
}

- (void)testTextFileUtf16
{
    FileEncoding e;
    std::string le("\xFF\xFE" "a\0\xE9\0" "=\xD8\0\xDE", 10);       // a, e-acute, U+1F600
    std::string text = TextFile::Decode(le, e);
    XCTAssertTrue(e == FileEncoding::Utf16LE);
    GRAssertEq(text, "a\xC3\xA9\xF0\x9F\x98\x80");
    GRAssertEq(TextFile::Encode(text, e), le);
    std::string be("\xFE\xFF" "\0a", 4);
    GRAssertEq(TextFile::Decode(be, e), "a");
    XCTAssertTrue(e == FileEncoding::Utf16BE);
    GRAssertEq(TextFile::Encode("a", e), be);
}

- (void)testTextFileWindows1252RoundTrip
{
    FileEncoding e;
    std::string bytes = "caf\xE9 \x80\r\n";
    std::string text = TextFile::Decode(bytes, e);
    XCTAssertTrue(e == FileEncoding::Windows1252);
    GRAssertEq(text, "caf\xC3\xA9 \xE2\x82\xAC\r\n");
    GRAssertEq(TextFile::Encode(text, e), bytes);
    GRAssertEq(TextFile::EncodingLabel(e), "WINDOWS-1252");
}

- (void)testTextFileKeepsCrlfOnDisk
{
    std::string path = GRTempDir() + "/crlf.c";
    std::string error;
    XCTAssertTrue(TextFile::Write(path, "a\r\nb\r\n", FileEncoding::Utf8, error));
    std::string text;
    FileEncoding e;
    XCTAssertTrue(TextFile::Read(path, text, e, error));
    GRAssertEq(text, "a\r\nb\r\n");
}

// ---- strings ----------------------------------------------------------------

- (void)testStringUtil
{
    GRAssertEq(StringUtil::Trim("  a b \t"), "a b");
    XCTAssertTrue(StringUtil::EqualsIgnoreCase("Kalman.C", "kalman.c"));
    XCTAssertTrue(StringUtil::StartsWithIgnoreCase("TMS320C6747", "tms"));
    GRAssertEq(StringUtil::IndexOfIgnoreCase("abcABC", "Bc", 2), 4);
    GRAssertEq(StringUtil::Replace("a-b-c", "-", "+"), "a+b+c");
    GRAssertEq(StringUtil::ReplaceIgnoreCase("aXbxc", "x", "_"), "a_b_c");
    GRAssertEq(StringUtil::Split("a,,b", ',').size(), (size_t)3);
    GRAssertEq(StringUtil::SplitNonEmpty("a;;b ;", "; ").size(), (size_t)2);
    GRAssertEq(StringUtil::SplitLines("a\r\nb\rc\nd").size(), (size_t)4);
    GRAssertEq(StringUtil::Join(", ", std::vector<std::string>{ "x", "y" }), "x, y");
    int n = 0;
    XCTAssertTrue(StringUtil::TryParseInt(" 42 ", n));
    GRAssertEq(n, 42);
    XCTAssertFalse(StringUtil::TryParseInt("4x", n));
    unsigned long long h = 0;
    XCTAssertTrue(StringUtil::TryParseHex("0x1F", h));
    GRAssertEq(h, 31ULL);
    CaseInsensitiveLess less;
    XCTAssertFalse(less("ABC", "abc") || less("abc", "ABC"));
}

// ---- command lines (Microsoft's rules) ---------------------------------------

- (void)testCommandLineSplitMicrosoftRules
{
    std::vector<std::string> w = Platform::CommandLine::Split("tool \"a b\" c\\\\\"d\" e\\f \"x\"\"y\" \\\\\\\"q");
    std::vector<std::string> expected{ "tool", "a b", "c\\d", "e\\f", "x\"y", "\\\"q" };
    GRAssertEq(w.size(), expected.size());
    for (size_t i = 0; i < w.size() && i < expected.size(); i++) GRAssertEq(w[i], expected[i]);
}

- (void)testCommandLineQuoteSplitsBack
{
    std::vector<std::string> words{ "/p/tool", "a b", "q\"r", "end\\", "" };
    std::vector<std::string> back = Platform::CommandLine::Split(Platform::CommandLine::Join(words));
    GRAssertEq(back.size(), words.size());
    for (size_t i = 0; i < back.size() && i < words.size(); i++) GRAssertEq(back[i], words[i]);
}

- (void)testSplitCommand
{
    std::string file, args;
    Platform::CommandLine::SplitCommand("\"/a b/tool\" -x -y", file, args);
    GRAssertEq(file, "/a b/tool");
    GRAssertEq(args, "-x -y");
    Platform::CommandLine::SplitCommand("tool", file, args);
    GRAssertEq(file, "tool");
    GRAssertEq(args, "");
}

@end
