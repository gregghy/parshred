#pragma once
/// @file dom.hpp
/// @brief DOM tree data structures for parshred.
///
/// Designed to beat RapidXML on small files while supporting 10 GB files.
///
/// Key design choices:
///   - 48-byte XmlNode (fits ~1.3 nodes per cache line)
///   - Intrusive linked-list for children/siblings (O(1) append, no vector)
///   - string_view for names/values (zero-copy into source buffer)
///   - Pool-allocated (one bump pointer increment per node)
///   - Type-punned attribute nodes (stored as sibling chain)

#include <parshred/common.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace parshred {

// ── Node Types ───────────────────────────────────────────────────────

enum class NodeType : uint8_t {
    Document = 0,       // Root document node
    Element,            // <name>...</name>
    Text,               // Character data between elements
    CData,              // <![CDATA[...]]>
    Comment,            // <!-- ... -->
    PI,                 // <?target data?>
    Attribute,          // name="value" (stored in attribute chain)
    Declaration,        // <?xml ...?>
    DocType,            // <!DOCTYPE ...>
};

// ── Node Flags ───────────────────────────────────────────────────────

enum NodeFlags : uint8_t {
    NF_NONE         = 0,
    NF_SELF_CLOSING = 1 << 0,   // Self-closing tag (<br/>)
    NF_HAS_CHILDREN = 1 << 1,   // Has child nodes
    NF_HAS_ATTRS    = 1 << 2,   // Has attributes
    NF_INSITU       = 1 << 3,   // Name/value modified in-situ
    NF_EXPANDED     = 1 << 4,   // Value had entities expanded (arena-alloc'd)
};

// ── XmlNode ──────────────────────────────────────────────────────────

/// Compact DOM node. 64 bytes to align nicely on cache lines.
///
/// Children are a linked list: first_child → next_sibling → next_sibling → ...
/// Attributes are a separate linked list: first_attr → next_sibling → ...
///
/// All string_views point into either:
///   - The original (possibly mutated) input buffer (in-situ mode)
///   - Arena-allocated storage (arena mode, for expanded entities)
struct XmlNode {
    // ── Data ─────────────────────────────── (40 bytes)
    std::string_view name{};        // 16 bytes: tag name or attr name
    std::string_view value{};       // 16 bytes: text content or attr value
    NodeType         type{};        //  1 byte
    uint8_t          flags{};       //  1 byte
    uint16_t         _pad0{};       //  2 bytes padding
    uint32_t         _pad1{};       //  4 bytes padding

    // ── Tree links ───────────────────────── (24 bytes)
    XmlNode*         parent{};      //  8 bytes
    XmlNode*         first_child{}; //  8 bytes
    XmlNode*         last_child{};  //  8 bytes (O(1) append)

    // ── Sibling/attr links ───────────────── (16 bytes)
    XmlNode*         next_sibling{}; // 8 bytes
    XmlNode*         first_attr{};   // 8 bytes (first attribute node)

    // Total: 80 bytes (slightly over one cache line, acceptable)

    // ── Convenience ──────────────────────────
    [[nodiscard]] bool is_element() const noexcept { return type == NodeType::Element; }
    [[nodiscard]] bool is_text()    const noexcept { return type == NodeType::Text; }
    [[nodiscard]] bool has_children() const noexcept { return first_child != nullptr; }
    [[nodiscard]] bool has_attrs() const noexcept { return first_attr != nullptr; }
};

static_assert(sizeof(XmlNode) == 80, "XmlNode should be 80 bytes");

// ── Document ─────────────────────────────────────────────────────────

/// Forward declarations for iteration helpers.
struct ChildRange;
struct AttrRange;

/// XmlDocument is the owner of all nodes. It holds the memory pool
/// and provides the API for navigating the tree.
///
/// Usage:
/// @code
///   auto doc = parshred::dom_parse_insitu(buffer.data(), buffer.size());
///   auto* root = doc.root();
///   for (auto* child : doc.children(root)) { ... }
/// @endcode
class XmlDocument {
public:
    XmlDocument() = default;
    ~XmlDocument() = default;

    // Move-only (owns the pool)
    XmlDocument(XmlDocument&&) noexcept = default;
    XmlDocument& operator=(XmlDocument&&) noexcept = default;
    XmlDocument(const XmlDocument&) = delete;
    XmlDocument& operator=(const XmlDocument&) = delete;

    /// The root element node (first element child of the document node).
    [[nodiscard]] XmlNode* root() const noexcept;

    /// The document node (parent of root, PIs, comments, doctype).
    [[nodiscard]] XmlNode* document_node() noexcept { return &doc_node_; }

    // ── Navigation ───────────────────────────────────────────────────

    /// First child element of `node` (skips text/comment/PI nodes).
    [[nodiscard]] static XmlNode* first_element(XmlNode* node) noexcept;

    /// Next sibling element (skips non-element nodes).
    [[nodiscard]] static XmlNode* next_element(XmlNode* node) noexcept;

    /// Find first child element with given name.
    [[nodiscard]] static XmlNode* find_child(XmlNode* node, std::string_view name) noexcept;

    /// Get attribute value by name, or empty string_view if not found.
    [[nodiscard]] static std::string_view attr(XmlNode* node, std::string_view name) noexcept;

    // ── Ranges for range-for ─────────────────────────────────────────

    /// Iterate all children of `node`.
    [[nodiscard]] static ChildRange children(XmlNode* node) noexcept;

    /// Iterate element children of `node`.
    [[nodiscard]] static ChildRange elements(XmlNode* node) noexcept;

    /// Iterate attributes of `node`.
    [[nodiscard]] static AttrRange attributes(XmlNode* node) noexcept;

    // ── Statistics ───────────────────────────────────────────────────

    [[nodiscard]] size_t node_count() const noexcept { return node_count_; }
    [[nodiscard]] size_t bytes_allocated() const noexcept { return bytes_allocated_; }

    // Public for access by dom_parse template (friend templates are fragile)
    XmlNode doc_node_{.type = NodeType::Document};
    size_t  node_count_ = 0;
    size_t  bytes_allocated_ = 0;
};

// ── Range helpers ────────────────────────────────────────────────────

/// Iterator over sibling nodes.
struct NodeIterator {
    XmlNode* node;
    bool     elements_only;

    NodeIterator& operator++() noexcept {
        if (node) {
            node = node->next_sibling;
            if (elements_only) {
                while (node && node->type != NodeType::Element)
                    node = node->next_sibling;
            }
        }
        return *this;
    }
    [[nodiscard]] XmlNode* operator*() const noexcept { return node; }
    [[nodiscard]] bool operator!=(const NodeIterator& o) const noexcept {
        return node != o.node;
    }
};

/// Range of child nodes.
struct ChildRange {
    XmlNode* first;
    bool     elements_only;

    [[nodiscard]] NodeIterator begin() const noexcept { return {first, elements_only}; }
    [[nodiscard]] NodeIterator end() const noexcept { return {nullptr, elements_only}; }
};

/// Iterator over attribute nodes.
struct AttrIterator {
    XmlNode* node;

    AttrIterator& operator++() noexcept {
        if (node) node = node->next_sibling;
        return *this;
    }
    [[nodiscard]] XmlNode* operator*() const noexcept { return node; }
    [[nodiscard]] bool operator!=(const AttrIterator& o) const noexcept {
        return node != o.node;
    }
};

/// Range of attributes.
struct AttrRange {
    XmlNode* first;

    [[nodiscard]] AttrIterator begin() const noexcept { return {first}; }
    [[nodiscard]] AttrIterator end() const noexcept { return {nullptr}; }
};

// ── Inline implementations ───────────────────────────────────────────

inline XmlNode* XmlDocument::root() const noexcept {
    return first_element(const_cast<XmlNode*>(&doc_node_));
}

inline XmlNode* XmlDocument::first_element(XmlNode* node) noexcept {
    if (!node) return nullptr;
    XmlNode* c = node->first_child;
    while (c && c->type != NodeType::Element)
        c = c->next_sibling;
    return c;
}

inline XmlNode* XmlDocument::next_element(XmlNode* node) noexcept {
    if (!node) return nullptr;
    XmlNode* n = node->next_sibling;
    while (n && n->type != NodeType::Element)
        n = n->next_sibling;
    return n;
}

inline XmlNode* XmlDocument::find_child(XmlNode* node, std::string_view name) noexcept {
    if (!node) return nullptr;
    for (XmlNode* c = node->first_child; c; c = c->next_sibling) {
        if (c->type == NodeType::Element && c->name == name)
            return c;
    }
    return nullptr;
}

inline std::string_view XmlDocument::attr(XmlNode* node, std::string_view name) noexcept {
    if (!node) return {};
    for (XmlNode* a = node->first_attr; a; a = a->next_sibling) {
        if (a->name == name) return a->value;
    }
    return {};
}

inline ChildRange XmlDocument::children(XmlNode* node) noexcept {
    return {node ? node->first_child : nullptr, false};
}

inline ChildRange XmlDocument::elements(XmlNode* node) noexcept {
    XmlNode* first = first_element(node);
    return {first, true};
}

inline AttrRange XmlDocument::attributes(XmlNode* node) noexcept {
    return {node ? node->first_attr : nullptr};
}

} // namespace parshred
