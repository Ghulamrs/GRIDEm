// MapFile.cpp - the object map in the layout of a Visual Studio linker map.
#include "MapFile.hpp"

#include <algorithm>
#include <ctime>
#include <map>
#include <sstream>
#include <vector>

namespace cppparse {

namespace {

std::string hexText(unsigned long v, int digits)
{
    static const char* const kDigits = "0123456789abcdef";
    std::string s(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0 && v; --i) { s[static_cast<size_t>(i)] = kDigits[v & 0xF]; v >>= 4; }
    return s;
}

std::string address(int section, unsigned long offset)
{
    return hexText(static_cast<unsigned long>(section), 4) + ":" + hexText(offset, 8);
}

std::string pad(const std::string& s, size_t width)
{
    return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
}

std::string typeOf(const CoffSymbolInfo& y, const std::string& readable)
{
    if (y.isFunction) return "function";
    if (readable.compare(0, 14, "string literal") == 0) return "string";
    if (readable.compare(0, 15, "double constant") == 0 || y.name == "sign_mask") return "constant";
    const std::string vftable = "`vftable'";
    if (readable.size() > vftable.size() && readable.compare(readable.size() - vftable.size(), vftable.size(), vftable) == 0) return "vtable";
    return "data";
}

// link's flags: "f" a function, "i" an inline one (in a COMDAT section of its own, one copy kept by the linker)
std::string flagsOf(const CoffResult& r, const CoffSymbolInfo& y)
{
    if (!y.isFunction) return "   ";
    for (size_t i = 0; i < r.sectionList.size(); ++i)
        if (r.sectionList[i].number == y.section) return r.sectionList[i].comdat ? "f i" : "f  ";
    return "f  ";
}

void symbolLines(std::ostringstream& s, const CoffResult& r, const std::map<std::string, std::string>& names, bool external)
{
    s << "  Address         Size        Flags  Type      Symbol                                    Name\n\n";
    int count = 0;
    for (size_t i = 0; i < r.symbolList.size(); ++i) {
        const CoffSymbolInfo& y = r.symbolList[i];
        if (y.external != external) continue;
        std::map<std::string, std::string>::const_iterator n = names.find(y.name);
        const std::string readable = n == names.end() ? std::string() : n->second;
        s << " " << address(y.section, y.offset) << "   " << hexText(y.size, 8) << "H   " << flagsOf(r, y) << "    " << pad(typeOf(y, readable), 9) << " "
          << pad(y.name, 41) << (readable == y.name ? std::string() : " " + readable) << "\n";
        ++count;
    }
    if (count == 0) s << "  (none)\n";
}

// a symbol of the by-name list: sorted by its readable name, or by its name in the object when it has none
struct NamedSymbol {
    const CoffSymbolInfo* y;
    std::string           readable;
    std::string           key;
};

struct ByNameOrder {
    bool operator()(const NamedSymbol& a, const NamedSymbol& b) const
    {
        if (a.key != b.key) return a.key < b.key;
        if (a.y->name != b.y->name) return a.y->name < b.y->name;
        return a.y->section != b.y->section ? a.y->section < b.y->section : a.y->offset < b.y->offset;
    }
};

// every symbol again, public and static, in name order: the address lists above answer "what is here", this one
// answers "where is it"
void byNameLines(std::ostringstream& s, const CoffResult& r, const std::map<std::string, std::string>& names)
{
    std::vector<NamedSymbol> list;
    for (size_t i = 0; i < r.symbolList.size(); ++i) {
        NamedSymbol n;
        n.y = &r.symbolList[i];
        std::map<std::string, std::string>::const_iterator it = names.find(n.y->name);
        n.readable = it == names.end() ? std::string() : it->second;
        n.key = n.readable.empty() ? n.y->name : n.readable;
        list.push_back(n);
    }
    std::sort(list.begin(), list.end(), ByNameOrder());
    s << "  Address         Size        Flags  Type      Linkage  Symbol                                    Name\n\n";
    for (size_t i = 0; i < list.size(); ++i) {
        const CoffSymbolInfo& y = *list[i].y;
        s << " " << address(y.section, y.offset) << "   " << hexText(y.size, 8) << "H   " << flagsOf(r, y) << "    " << pad(typeOf(y, list[i].readable), 9) << " "
          << pad(y.external ? "public" : "static", 8) << " " << pad(y.name, 41)
          << (list[i].readable == y.name ? std::string() : " " + list[i].readable) << "\n";
    }
    if (list.empty()) s << "  (none)\n";
}

} // namespace

std::string renderObjectMap(const std::string& title, const std::string& object, const CoffResult& r,
                            const std::map<std::string, std::string>& names)
{
    std::ostringstream s;
    s << " " << title << "\n\n";
    // as link /MAP: the time in seconds since 1970 (hex), then the local date; the object itself keeps a zero
    // time stamp so that it stays the same from run to run
    time_t now = time(0);
    char when[64] = "";
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)      // MSVC deprecates localtime for its localtime_s, which is not C++98
#endif
    const struct tm* local = localtime(&now);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    if (local && strftime(when, sizeof when, "%a %b %d %H:%M:%S %Y", local) > 9 && when[8] == '0') when[8] = ' ';   // "Oct  9", as ctime
    s << " Timestamp is " << hexText(static_cast<unsigned long>(now) & 0xFFFFFFFFUL, 8) << " (" << when << ")\n\n";
    s << " cppparse object map of " << object << "\n";
    s << " Addresses are section:offset in the object; sizes are the encoded bytes of each definition.\n\n";
    if (!r.errors.empty()) {
        s << " not available: " << r.errors.size() << " line(s) of the assembly could not be encoded\n";
        for (size_t i = 0; i < r.errors.size() && i < 10; ++i) s << "   " << r.errors[i] << "\n";
        return s.str();
    }

    // the plain sections one per line; COMDAT sections (one per inline function, template instance or vtable)
    // grouped by name and class: the first and last section number, how many, and their total size
    s << " Start         Length     Name                   Class\n";
    unsigned long code = 0, data = 0;
    for (size_t i = 0; i < r.sectionList.size(); ++i) {
        const CoffSectionInfo& c = r.sectionList[i];
        if (c.isCode) code += c.size; else data += c.size;
        if (!c.comdat) s << " " << address(c.number, 0) << " " << hexText(c.size, 8) << "H  " << pad(c.name, 22) << " " << (c.isCode ? "CODE" : "DATA") << "\n";
    }
    std::vector<std::string> groupOrder;
    std::map<std::string, std::pair<int, int> > groupRange;      // name/class -> first, last section number
    std::map<std::string, std::pair<int, unsigned long> > groupSize;   // count, bytes
    for (size_t i = 0; i < r.sectionList.size(); ++i) {
        const CoffSectionInfo& c = r.sectionList[i];
        if (!c.comdat) continue;
        std::string key = pad(c.name, 22) + " " + (c.isCode ? "CODE" : "DATA");
        if (!groupRange.count(key)) { groupOrder.push_back(key); groupRange[key] = std::make_pair(c.number, c.number); groupSize[key] = std::make_pair(0, 0UL); }
        groupRange[key].second = c.number;
        groupSize[key].first++;
        groupSize[key].second += c.size;
    }
    for (size_t g = 0; g < groupOrder.size(); ++g) {
        const std::string& key = groupOrder[g];
        s << " " << hexText(static_cast<unsigned long>(groupRange[key].first), 4) << "-" << hexText(static_cast<unsigned long>(groupRange[key].second), 4)
          << "     " << hexText(groupSize[key].second, 8) << "H  " << key << "  COMDAT, " << groupSize[key].first << " section(s)\n";
    }
    int functions = 0, publics = 0;
    for (size_t i = 0; i < r.symbolList.size(); ++i) { if (r.symbolList[i].isFunction) ++functions; if (r.symbolList[i].external) ++publics; }
    s << "\n Totals: code " << code << " bytes, data " << data << " bytes; " << r.symbolList.size() << " symbols ("
      << functions << " functions), " << publics << " public, " << (r.symbolList.size() - static_cast<size_t>(publics)) << " static\n\n";

    s << " Publics by Value\n\n";
    symbolLines(s, r, names, true);
    s << "\n Static symbols\n\n";
    symbolLines(s, r, names, false);

    for (size_t i = 0; i < r.symbolList.size(); ++i)
        if (r.symbolList[i].isFunction && r.symbolList[i].name == "main") {
            s << "\n entry point at        " << address(r.symbolList[i].section, r.symbolList[i].offset) << "\n";
            break;
        }
    s << "\n Symbols by Name\n\n";
    byNameLines(s, r, names);
    return s.str();
}

} // namespace cppparse
