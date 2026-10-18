# GRIDEm - architecture

The class map is the reference's: [`GRIDEm-Design.md`](GRIDEm-Design.md), sections 2 (Core, Platform,
Model, Settings, Services: GRIDEd's classes one for one in C++14) and 4 (the UI: GRIDEd's WinForms
classes and the AppKit classes that stand for them), with the main flows in section 5. Each source
file's header comment says which GRIDEd file it mirrors.

Layers, each using only the ones above it:

| Layer | Folder | Namespace |
|---|---|---|
| Core | `Sources/Core` | `GRIDEm::Core` |
| Platform | `Sources/Platform` | `GRIDEm::Platform` |
| Settings | `Sources/Settings` | `GRIDEm::Settings` |
| Model | `Sources/Model` | `GRIDEm::Model` |
| Services | `Sources/Services` | `GRIDEm::Services` |
| UI | `Sources/UI` | Objective-C++ classes prefixed `GR` |

Threading: services report on their own threads; `UI/Session.mm` (`GRSession`) moves every event to the
main queue. No view touches a service directly.
