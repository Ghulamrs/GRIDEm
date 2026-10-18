# Porting GRIDEd to GRIDEm - the working rules

The reference is `GRIDEm-Design.md` (the agreed contract): C++14 only, in the plain
style of its section 0a. These are the rules every ported file follows, so the code
reads the same everywhere and can be laid beside GRIDEd's.

## Files and names

- One GRIDEd file becomes one `.h` and one `.cpp` of the same base name in the same layer
  folder (`Settings/ToolchainDefinitions.h` -> `Sources/Settings/ToolchainDefinitions.h/.cpp`).
- The classes keep GRIDEd's names and members; namespaces become
  `GRIDEm::Core`, `GRIDEm::Platform`, `GRIDEm::Settings`, `GRIDEm::Model`,
  `GRIDEm::Services`, `GRIDEm::UI`.
- Each header starts with a comment: what it is, and which GRIDEd file it mirrors.
- Includes are relative, as GRIDEd's are: `#include "../Core/Json.h"`.
- The product is always written **GRIDEm** (and GRIDEd, GRIDE) - never "Gridem".

## Types

| GRIDEd (C++/CLI) | GRIDEm (C++14) |
|---|---|
| `String^` | `std::string` (UTF-8). Where GRIDEd tells `nullptr` from `""`, a `bool` beside it or a `bool Find(..., std::string& out)` |
| `List<T>^` | `std::vector<T>` |
| `Dictionary<String^, T>^` | `std::map<std::string, T>`; with `StringComparer::OrdinalIgnoreCase`, `std::map<std::string, T, Core::CaseInsensitiveLess>` |
| `SortedSet<int>^` | `std::set<int>` |
| `ref class X` held and shared by handle | `class X`, held by `std::shared_ptr<X>` (`typedef std::shared_ptr<X> XPtr;`) |
| `ref class X` used as a value | a plain class or struct, copied |
| `property T Name;` (auto property) | a public member `T Name;` |
| `property T Name { T get() ... }` | a method `T Name() const` |
| `event H^ Something;` | a public `std::function<...> OnSomething;` member, called when set |
| `String::Format`, `+` | `+` and `std::to_string` |
| `Regex` | `std::regex` (ECMAScript): no look-behind and no named groups - write those parts by hand |
| `File`, `Directory`, `Path` | `Platform::Paths`, `Core::PathUtil` |
| `Process`, job objects, the pseudo-console | `Platform::Process`, `Platform::JobGroup`, `Platform::Pty` |
| `Environment::ExpandEnvironmentVariables` | `Platform::Environment::Expand` |
| exceptions | `std::runtime_error` (`Core::JsonException` for JSON) |
| `Thread`, `Monitor` | `std::thread`, `std::mutex`, `std::condition_variable` - in Platform and Services only |
| `BeginInvoke` to the UI | Services call their `std::function` handlers on their own thread; `UI/Session.mm` moves them to the main queue |

## Windows-only parts

`VisualStudio`, `cl`, `cdb`, `vcvars`: the code stays (so settings and projects keep their
fields and write them back), but on the Mac it answers "Windows only" / "not on this machine".
Settings keys GRIDEm does not use are kept and written back unchanged.

## Checking

`make check` compiles every C++ source alone with `-std=c++14 -pedantic -Werror`;
`make lib` builds `build/libGRIDEm.a`. Both run on the Linux shell as well as on the Mac.
GRIDEm's tests are XCTest (`Tests/`), run on the Mac.
