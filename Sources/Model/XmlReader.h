// =============================================================================
//  XmlReader.h - a small XML reader for the project files GRIDEm reads and
//  never writes: CCS's .project, .cproject, .ccsproject and the workspace's
//  macros; Visual Studio's .vcxproj, .vcxproj.filters and .vcproj.
//  Stands in for the parts of System::Xml (XmlDocument, XmlNode, SelectNodes)
//  GRIDEd's readers use: elements, attributes, text, entities, CDATA,
//  comments and processing instructions (skipped), and the simple paths
//  "a/b", "/a/b", "//a", ".//a/b".
//  New in GRIDEm (there is no System::Xml on the Mac).
// =============================================================================
#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace GRIDEm { namespace Model {

    class XmlNode;
    typedef std::shared_ptr<XmlNode> XmlNodePtr;

    // one element: its name, attributes in document order, child elements and text
    class XmlNode
    {
    public:
        std::string Name;                                          // as written, with any prefix
        std::vector<std::pair<std::string, std::string>> Attributes;
        std::vector<XmlNodePtr> Children;                          // elements only
        std::string Text;                                          // this element's own text, in order

        // the name without a namespace prefix (XmlNode::LocalName)
        std::string LocalName() const;
        // an attribute's value, "" when it is not there (names compared exactly, as System::Xml does)
        std::string Attr(const std::string& name) const;
        bool HasAttr(const std::string& name) const;
        // all text inside, descendants' included, in document order (XmlNode::InnerText)
        std::string InnerText() const;

        // the paths GRIDEd uses: "name", "a/b", ".//a", ".//a/b", "//a", "/a/b" (from the document root)
        std::vector<XmlNodePtr> SelectNodes(const std::string& path) const;
        XmlNodePtr SelectSingleNode(const std::string& path) const;   // nullptr when none

        // the first child element with this local name, ignoring case; nullptr when none
        XmlNodePtr Child(const std::string& localName) const;

    private:
        friend class XmlDocument;
        friend class XmlParser;
        XmlNode* parent = nullptr;
        // the text with its child elements' text in between, in document order
        std::vector<std::pair<int, std::string>> pieces;           // child index (-1 for text) and text
        const XmlNode* Root() const;
    };

    class XmlException : public std::runtime_error
    {
    public:
        explicit XmlException(const std::string& msg) : std::runtime_error(msg) {}
    };

    class XmlDocument
    {
    public:
        XmlNodePtr DocumentElement;                                // nullptr for an empty document

        // throws XmlException with the line of the fault
        static XmlDocument Parse(const std::string& text);
        static XmlDocument Load(const std::string& path);          // throws XmlException (also when unreadable)

        std::vector<XmlNodePtr> SelectNodes(const std::string& path) const;
        XmlNodePtr SelectSingleNode(const std::string& path) const;
    };
}}
