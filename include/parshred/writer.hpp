#pragma once
/// @file writer.hpp
/// @brief XML writer/serializer + DOM tree modification API.
///
/// Features:
///   - Serialize FastDom to well-formed XML string
///   - Pretty-print or compact output
///   - Tree modification (add/remove elements, attributes)
///   - XML document builder (programmatic construction)

#include <parshred/dom_fast.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace parshred {

// ── Serialization Options ────────────────────────────────────────────

struct WriteOptions {
    bool pretty = true;             // Enable indentation
    std::string indent = "  ";      // Indent string (2 spaces default)
    bool xml_declaration = true;    // Write <?xml ...?> header
    std::string encoding = "UTF-8"; // Encoding in declaration
    bool self_close_empty = true;   // Use <br/> vs <br></br>
    bool escape_entities = true;    // Escape &, <, >, etc.
};

// ── Entity Escaping ──────────────────────────────────────────────────

/// Escape a string for use in XML text content.
inline std::string escape_text(std::string_view text) {
    std::string result;
    result.reserve(text.size() + text.size() / 8);
    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            default:   result += c;        break;
        }
    }
    return result;
}

/// Escape a string for use in XML attribute values.
inline std::string escape_attr(std::string_view text) {
    std::string result;
    result.reserve(text.size() + text.size() / 8);
    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;";   break;
            case '<':  result += "&lt;";    break;
            case '"':  result += "&quot;";  break;
            case '\'': result += "&apos;";  break;
            default:   result += c;         break;
        }
    }
    return result;
}

// ── XML Writer ───────────────────────────────────────────────────────

/// Serialize a FastDom tree to an XML string.
class XmlWriter {
public:
    explicit XmlWriter(const WriteOptions& opts = {}) : opts_(opts) {}

    /// Serialize the entire document.
    [[nodiscard]] std::string serialize(const FastDom& dom) const {
        std::string out;
        out.reserve(dom.node_count * 40);  // Rough estimate

        if (opts_.xml_declaration) {
            out += "<?xml version=\"1.0\" encoding=\"";
            out += opts_.encoding;
            out += "\"?>";
            if (opts_.pretty) out += '\n';
        }

        if (dom.root_idx > 0 && dom.root_idx < dom.node_count) {
            serialize_node(dom, dom.root_idx, out, 0);
        }

        return out;
    }

    /// Serialize a single node and its subtree.
    [[nodiscard]] std::string serialize_node_str(const FastDom& dom, uint32_t idx) const {
        std::string out;
        serialize_node(dom, idx, out, 0);
        return out;
    }

private:
    WriteOptions opts_;

    void serialize_node(const FastDom& dom, uint32_t idx, std::string& out, int depth) const {
        const auto& node = dom.nodes[idx];

        switch (node.type) {
            case 1:  // Element
                serialize_element(dom, idx, out, depth);
                break;
            case 2:  // Text
                serialize_text(dom, idx, out);
                break;
            case 3:  // Comment
                write_indent(out, depth);
                out += "<!--";
                out += std::string(dom.value(node));
                out += "-->";
                if (opts_.pretty) out += '\n';
                break;
            case 4:  // CDATA
                out += "<![CDATA[";
                out += std::string(dom.value(node));
                out += "]]>";
                break;
            case 5:  // PI
                write_indent(out, depth);
                out += "<?";
                out += std::string(dom.name(node));
                auto pival = dom.value(node);
                if (!pival.empty()) {
                    out += ' ';
                    out += std::string(pival);
                }
                out += "?>";
                if (opts_.pretty) out += '\n';
                break;
        }
    }

    void serialize_element(const FastDom& dom, uint32_t idx, std::string& out, int depth) const {
        const auto& node = dom.nodes[idx];
        auto name = dom.name(node);

        write_indent(out, depth);
        out += '<';
        out += std::string(name);

        // Write attributes
        uint32_t attr = node.first_attr;
        while (attr) {
            const auto& a = dom.nodes[attr];
            out += ' ';
            out += std::string(dom.name(a));
            out += "=\"";
            if (opts_.escape_entities) {
                out += escape_attr(dom.value(a));
            } else {
                out += std::string(dom.value(a));
            }
            out += '"';
            attr = a.next_sibling;
        }

        // Check for children
        if (node.first_child == 0) {
            // Empty element
            if (opts_.self_close_empty) {
                out += "/>";
            } else {
                out += "></";
                out += std::string(name);
                out += '>';
            }
            if (opts_.pretty) out += '\n';
            return;
        }

        out += '>';

        // Check if only text children (for inline text)
        bool has_element_children = false;
        uint32_t child = node.first_child;
        while (child) {
            if (dom.nodes[child].type == 1) {
                has_element_children = true;
                break;
            }
            child = dom.nodes[child].next_sibling;
        }

        if (has_element_children && opts_.pretty) {
            out += '\n';
        }

        // Write children
        child = node.first_child;
        while (child) {
            if (has_element_children) {
                serialize_node(dom, child, out, depth + 1);
            } else {
                // Inline text — no indent
                serialize_text(dom, child, out);
            }
            child = dom.nodes[child].next_sibling;
        }

        // Close tag
        if (has_element_children) {
            write_indent(out, depth);
        }
        out += "</";
        out += std::string(name);
        out += '>';
        if (opts_.pretty) out += '\n';
    }

    void serialize_text(const FastDom& dom, uint32_t idx, std::string& out) const {
        const auto& node = dom.nodes[idx];
        auto text = dom.value(node);
        if (opts_.escape_entities) {
            out += escape_text(text);
        } else {
            out += std::string(text);
        }
    }

    void write_indent(std::string& out, int depth) const {
        if (!opts_.pretty) return;
        for (int i = 0; i < depth; ++i) {
            out += opts_.indent;
        }
    }
};

// ── DOM Builder ──────────────────────────────────────────────────────

/// Programmatic XML document builder.
/// Creates a FastDom tree from scratch.
class DomBuilder {
public:
    DomBuilder() {
        // Sentinel + document node
        nodes_.push_back(FastNode{});  // idx 0 = null
        nodes_.push_back(FastNode{.type = 0});  // idx 1 = document
        stack_.push_back({1, 0});
    }

    /// Start an element. Returns its index.
    uint32_t start_element(std::string_view name) {
        uint32_t idx = alloc_node(1);
        set_name(idx, name);
        link_child(idx);
        stack_.push_back({idx, 0});
        return idx;
    }

    /// End the current element.
    void end_element() {
        if (stack_.size() > 1) stack_.pop_back();
    }

    /// Add a self-closing element.
    uint32_t add_element(std::string_view name) {
        uint32_t idx = alloc_node(1);
        set_name(idx, name);
        link_child(idx);
        return idx;
    }

    /// Add an attribute to the most recently opened element.
    void add_attribute(std::string_view name, std::string_view value) {
        if (stack_.size() <= 1) return;
        uint32_t elem_idx = stack_.back().node_idx;
        uint32_t attr_idx = alloc_node(6);
        set_name(attr_idx, name);
        set_value(attr_idx, value);

        auto& elem = nodes_[elem_idx];
        if (elem.first_attr == 0) {
            elem.first_attr = attr_idx;
        } else {
            // Find last attr
            uint32_t last = elem.first_attr;
            while (nodes_[last].next_sibling) last = nodes_[last].next_sibling;
            nodes_[last].next_sibling = attr_idx;
        }
    }

    /// Add text content to the current element.
    void add_text(std::string_view text) {
        uint32_t idx = alloc_node(2);
        set_value(idx, text);
        nodes_[idx].flags = 0x01;  // Value in values buffer
        link_child(idx);
    }

    /// Add a comment.
    void add_comment(std::string_view text) {
        uint32_t idx = alloc_node(3);
        set_value(idx, text);
        nodes_[idx].flags = 0x01;
        link_child(idx);
    }

    /// Finalize and return the built DOM.
    /// The builder is consumed (moved from).
    [[nodiscard]] FastDom build() {
        FastDom dom;
        dom.node_count = nodes_.size();
        dom.capacity = nodes_.size();
        dom.nodes = static_cast<FastNode*>(std::malloc(nodes_.size() * sizeof(FastNode)));
        if (!dom.nodes) throw std::bad_alloc();
        std::memcpy(dom.nodes, nodes_.data(), nodes_.size() * sizeof(FastNode));
        dom.values = std::move(values_);
        dom.data_ptr = nullptr;  // All values are in the values buffer

        // Find root element
        uint32_t child = dom.nodes[1].first_child;
        while (child) {
            if (dom.nodes[child].type == 1) {
                dom.root_idx = child;
                break;
            }
            child = dom.nodes[child].next_sibling;
        }

        // Fix name pointers: they point into names_ strings
        // We need them to remain valid — store in a persistent buffer
        // Actually, names_ is moved into dom via our trick below.
        // Let's store name data into values_ too and update pointers.
        
        // Actually, the names are already stored in names_ and the pointers
        // in the nodes point into those strings. After build(), the names_
        // vector must stay alive. Let's copy them into the values buffer.
        size_t old_values_size = dom.values.size();
        for (auto& s : names_) {
            dom.values.insert(dom.values.end(), s.begin(), s.end());
        }
        // Repoint name_ptr to the values buffer
        size_t offset = old_values_size;
        size_t name_idx = 0;
        for (size_t i = 0; i < dom.node_count; ++i) {
            if (dom.nodes[i].name_len > 0 && name_idx < name_offsets_.size()) {
                dom.nodes[i].name_ptr = dom.values.data() + offset + name_offsets_[name_idx];
                ++name_idx;
            }
        }

        return dom;
    }

private:
    struct StackEntry {
        uint32_t node_idx;
        uint32_t last_child;
    };

    std::vector<FastNode> nodes_;
    std::vector<char> values_;
    std::vector<std::string> names_;
    std::vector<size_t> name_offsets_;  // Offset of each name in concatenated names_
    std::vector<StackEntry> stack_;

    uint32_t alloc_node(uint8_t type) {
        uint32_t idx = static_cast<uint32_t>(nodes_.size());
        FastNode n{};
        n.type = type;
        nodes_.push_back(n);
        return idx;
    }

    void set_name(uint32_t idx, std::string_view name) {
        // Calculate offset: sum of all previous names' sizes
        size_t offset = 0;
        for (auto& s : names_) offset += s.size();
        name_offsets_.push_back(offset);
        names_.push_back(std::string(name));
        nodes_[idx].name_ptr = names_.back().data();
        nodes_[idx].name_len = static_cast<uint32_t>(name.size());
    }

    void set_value(uint32_t idx, std::string_view value) {
        nodes_[idx].value_offset = static_cast<uint32_t>(values_.size());
        nodes_[idx].value_len = static_cast<uint16_t>(std::min(value.size(), size_t(65535)));
        values_.insert(values_.end(), value.begin(), value.end());
    }

    void link_child(uint32_t child_idx) {
        auto& top = stack_.back();
        if (top.last_child) {
            nodes_[top.last_child].next_sibling = child_idx;
        } else {
            nodes_[top.node_idx].first_child = child_idx;
        }
        top.last_child = child_idx;
    }
};

// ── Tree Modification ────────────────────────────────────────────────

/// Remove a child element from a parent in a FastDom.
/// Note: This unlinks the node but doesn't free memory (nodes array is flat).
inline void remove_child(FastDom& dom, uint32_t parent_idx, uint32_t child_idx) {
    auto& parent = dom.nodes[parent_idx];
    if (parent.first_child == child_idx) {
        parent.first_child = dom.nodes[child_idx].next_sibling;
        return;
    }
    // Find the previous sibling
    uint32_t prev = parent.first_child;
    while (prev && dom.nodes[prev].next_sibling != child_idx) {
        prev = dom.nodes[prev].next_sibling;
    }
    if (prev) {
        dom.nodes[prev].next_sibling = dom.nodes[child_idx].next_sibling;
    }
}

/// Remove an attribute from an element.
inline void remove_attribute(FastDom& dom, uint32_t elem_idx, std::string_view attr_name) {
    auto& elem = dom.nodes[elem_idx];
    uint32_t prev = 0;
    uint32_t curr = elem.first_attr;
    while (curr) {
        if (dom.name(dom.nodes[curr]) == attr_name) {
            if (prev == 0) {
                elem.first_attr = dom.nodes[curr].next_sibling;
            } else {
                dom.nodes[prev].next_sibling = dom.nodes[curr].next_sibling;
            }
            return;
        }
        prev = curr;
        curr = dom.nodes[curr].next_sibling;
    }
}

} // namespace parshred
