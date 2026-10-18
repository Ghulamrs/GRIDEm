// cxxlink: a C++ program that needs the C++ runtime - new, delete, a virtual call, a
// dynamic_cast. Linked with cc alone it misses operator new, the type_info vtables and
// __gxx_personality_v0; linked with c++ it runs and returns 0.
#include <cstdio>

struct Shape { virtual ~Shape() {} virtual int sides() const = 0; };
struct Square : Shape { int sides() const { return 4; } };

int main() {
    Shape* s = new Square();
    int n = s->sides();
    Square* q = dynamic_cast<Square*>(s);
    delete s;
    std::printf("%d %s\n", n, q != 0 ? "square" : "?");
    return n == 4 ? 0 : 1;
}
