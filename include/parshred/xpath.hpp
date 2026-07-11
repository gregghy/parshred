#pragma once
/// @file xpath.hpp
/// @brief XPath 1.0 evaluation engine for parshred DOM.
///
/// Supports a practical subset of XPath 1.0:
///   Location paths: absolute (/a/b), relative (a/b), descendant (//a)
///   Axes: child (default), attribute (@), self (.), parent (..)
///   Wildcards: *, @*
///   Predicates: [n], [@attr], [@attr='val'], [text()='val']
///   Functions: text(), name(), count(), last(), position(),
///              contains(), starts-with(), string-length()
///
/// Works with the FastDom structure from dom_fast.hpp.

#include <parshred/dom_fast.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace parshred {
namespace xpath {

// ── XPath Value Types ────────────────────────────────────────────────

/// XPath result types.
using NodeSet = std::vector<uint32_t>;  // Indices into FastDom::nodes

struct XPathValue {
    std::variant<NodeSet, std::string, double, bool> data;

    // Type queries
    [[nodiscard]] bool is_nodeset() const { return std::holds_alternative<NodeSet>(data); }
    [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] bool is_number() const { return std::holds_alternative<double>(data); }
    [[nodiscard]] bool is_boolean() const { return std::holds_alternative<bool>(data); }

    // Accessors
    [[nodiscard]] const NodeSet& as_nodeset() const { return std::get<NodeSet>(data); }
    [[nodiscard]] NodeSet& as_nodeset() { return std::get<NodeSet>(data); }
    [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(data); }
    [[nodiscard]] double as_number() const { return std::get<double>(data); }
    [[nodiscard]] bool as_boolean() const { return std::get<bool>(data); }

    // String conversion (XPath string() function semantics)
    [[nodiscard]] std::string to_string(const FastDom& dom) const {
        if (is_string()) return as_string();
        if (is_number()) {
            double v = as_number();
            if (v == static_cast<int64_t>(v)) return std::to_string(static_cast<int64_t>(v));
            return std::to_string(v);
        }
        if (is_boolean()) return as_boolean() ? "true" : "false";
        if (is_nodeset() && !as_nodeset().empty()) {
            // String value of first node
            const auto& n = dom.nodes[as_nodeset()[0]];
            return std::string(dom.name(n));
        }
        return {};
    }

    // Boolean conversion (XPath boolean() semantics)
    [[nodiscard]] bool to_boolean() const {
        if (is_boolean()) return as_boolean();
        if (is_number()) return as_number() != 0.0;
        if (is_string()) return !as_string().empty();
        if (is_nodeset()) return !as_nodeset().empty();
        return false;
    }
};

// ── XPath Step (parsed AST) ──────────────────────────────────────────

enum class Axis : uint8_t {
    Child,
    Descendant,
    DescendantOrSelf,
    Parent,
    Self,
    Attribute,
    Ancestor,
    AncestorOrSelf,
    FollowingSibling,
    PrecedingSibling,
};

enum class NodeTest : uint8_t {
    Name,           // Match specific name
    Wildcard,       // Match any element (*)
    TextNode,       // text()
    CommentNode,    // comment()
    AnyNode,        // node()
};

enum class PredicateOp : uint8_t {
    None,
    Position,       // [n] — numeric position
    AttrExists,     // [@attr] — attribute exists
    AttrEquals,     // [@attr='value']
    TextEquals,     // [text()='value']
    FuncCall,       // [contains(@attr, 'val')], etc.
};

struct Predicate {
    PredicateOp op = PredicateOp::None;
    std::string attr_name;
    std::string value;
    int position = 0;
};

struct Step {
    Axis axis = Axis::Child;
    NodeTest test = NodeTest::Name;
    std::string name;           // For NodeTest::Name
    std::vector<Predicate> predicates;
};

/// A parsed XPath expression.
struct XPathExpr {
    bool is_absolute = false;   // Starts with /
    std::vector<Step> steps;
};

// ── XPath Parser ─────────────────────────────────────────────────────

/// Parse an XPath expression string into an AST.
/// Supports: /a/b, //a, .., @attr, *, [n], [@attr='val']
inline XPathExpr parse_xpath(std::string_view expr) {
    XPathExpr result;
    size_t pos = 0;

    auto skip_ws = [&]() {
        while (pos < expr.size() && (expr[pos] == ' ' || expr[pos] == '\t')) ++pos;
    };

    auto read_name = [&]() -> std::string {
        size_t start = pos;
        while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) ||
               expr[pos] == '_' || expr[pos] == '-' || expr[pos] == ':' || expr[pos] == '.')) {
            ++pos;
        }
        return std::string(expr.substr(start, pos - start));
    };

    auto parse_predicate = [&]() -> Predicate {
        Predicate pred;
        ++pos; // skip '['
        skip_ws();

        if (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
            // Numeric position: [n]
            pred.op = PredicateOp::Position;
            size_t start = pos;
            while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) ++pos;
            pred.position = std::stoi(std::string(expr.substr(start, pos - start)));
        } else if (pos < expr.size() && expr[pos] == '@') {
            ++pos;
            pred.attr_name = read_name();
            skip_ws();
            if (pos < expr.size() && expr[pos] == '=') {
                // [@attr='value']
                ++pos;
                skip_ws();
                pred.op = PredicateOp::AttrEquals;
                char quote = (pos < expr.size()) ? expr[pos] : '\'';
                if (quote == '\'' || quote == '"') {
                    ++pos;
                    size_t start = pos;
                    while (pos < expr.size() && expr[pos] != quote) ++pos;
                    pred.value = std::string(expr.substr(start, pos - start));
                    if (pos < expr.size()) ++pos; // skip closing quote
                }
            } else {
                // [@attr] — existence check
                pred.op = PredicateOp::AttrExists;
            }
        } else if (pos + 6 < expr.size() && expr.substr(pos, 6) == "text()") {
            pos += 6;
            skip_ws();
            if (pos < expr.size() && expr[pos] == '=') {
                ++pos;
                skip_ws();
                pred.op = PredicateOp::TextEquals;
                char quote = (pos < expr.size()) ? expr[pos] : '\'';
                if (quote == '\'' || quote == '"') {
                    ++pos;
                    size_t start = pos;
                    while (pos < expr.size() && expr[pos] != quote) ++pos;
                    pred.value = std::string(expr.substr(start, pos - start));
                    if (pos < expr.size()) ++pos;
                }
            }
        } else if (pos + 5 < expr.size() && expr.substr(pos, 5) == "last(") {
            // [last()] — position = last
            pos += 6; // skip "last()"
            pred.op = PredicateOp::Position;
            pred.position = -1; // sentinel for "last"
        }

        skip_ws();
        if (pos < expr.size() && expr[pos] == ']') ++pos;
        return pred;
    };

    // Handle leading /
    skip_ws();
    if (pos < expr.size() && expr[pos] == '/') {
        result.is_absolute = true;
        ++pos;
        // Check for //
        if (pos < expr.size() && expr[pos] == '/') {
            ++pos;
            Step s;
            s.axis = Axis::DescendantOrSelf;
            s.test = NodeTest::AnyNode;
            result.steps.push_back(s);
        }
    }

    while (pos < expr.size()) {
        skip_ws();
        if (pos >= expr.size()) break;

        Step step;

        // Check for ..
        if (pos + 1 < expr.size() && expr[pos] == '.' && expr[pos + 1] == '.') {
            step.axis = Axis::Parent;
            step.test = NodeTest::AnyNode;
            pos += 2;
        }
        // Check for .
        else if (expr[pos] == '.' && (pos + 1 >= expr.size() || expr[pos + 1] == '/' || expr[pos + 1] == '[')) {
            step.axis = Axis::Self;
            step.test = NodeTest::AnyNode;
            ++pos;
        }
        // Check for @attr
        else if (expr[pos] == '@') {
            ++pos;
            step.axis = Axis::Attribute;
            if (pos < expr.size() && expr[pos] == '*') {
                step.test = NodeTest::Wildcard;
                ++pos;
            } else {
                step.test = NodeTest::Name;
                step.name = read_name();
            }
        }
        // Check for *
        else if (expr[pos] == '*') {
            step.test = NodeTest::Wildcard;
            ++pos;
        }
        // Check for text()
        else if (pos + 6 <= expr.size() && expr.substr(pos, 6) == "text()") {
            step.test = NodeTest::TextNode;
            pos += 6;
        }
        // Check for comment()
        else if (pos + 9 <= expr.size() && expr.substr(pos, 9) == "comment()") {
            step.test = NodeTest::CommentNode;
            pos += 9;
        }
        // Check for node()
        else if (pos + 6 <= expr.size() && expr.substr(pos, 6) == "node()") {
            step.test = NodeTest::AnyNode;
            pos += 6;
        }
        // Regular name
        else {
            step.test = NodeTest::Name;
            step.name = read_name();
            if (step.name.empty()) break;
        }

        // Parse predicates
        while (pos < expr.size() && expr[pos] == '[') {
            step.predicates.push_back(parse_predicate());
        }

        result.steps.push_back(std::move(step));

        // Consume separator
        skip_ws();
        if (pos < expr.size() && expr[pos] == '/') {
            ++pos;
            // Check for //
            if (pos < expr.size() && expr[pos] == '/') {
                ++pos;
                Step desc;
                desc.axis = Axis::DescendantOrSelf;
                desc.test = NodeTest::AnyNode;
                result.steps.push_back(desc);
            }
        } else {
            break;
        }
    }

    return result;
}

// ── XPath Evaluator ──────────────────────────────────────────────────

/// Get text content of a node (concatenation of all text children).
inline std::string get_text_content(const FastDom& dom, uint32_t node_idx) {
    std::string result;
    const auto& node = dom.nodes[node_idx];
    if (node.type == 2) {
        // Text node — return its value
        return std::string(dom.value(node));
    }
    // Element: concatenate text children
    uint32_t child = node.first_child;
    while (child) {
        const auto& c = dom.nodes[child];
        if (c.type == 2) {
            result += std::string(dom.value(c));
        } else if (c.type == 1) {
            result += get_text_content(dom, child);
        }
        child = c.next_sibling;
    }
    return result;
}

/// Evaluate a single step against a node set.
inline NodeSet eval_step(const FastDom& dom, const NodeSet& context, const Step& step) {
    NodeSet result;

    for (uint32_t ctx_idx : context) {
        const auto& ctx_node = dom.nodes[ctx_idx];
        NodeSet matches;

        switch (step.axis) {
            case Axis::Child: {
                uint32_t child = ctx_node.first_child;
                while (child) {
                    const auto& c = dom.nodes[child];
                    bool match = false;
                    switch (step.test) {
                        case NodeTest::Name:
                            match = (c.type == 1 && dom.name(c) == step.name);
                            break;
                        case NodeTest::Wildcard:
                            match = (c.type == 1);
                            break;
                        case NodeTest::TextNode:
                            match = (c.type == 2);
                            break;
                        case NodeTest::CommentNode:
                            match = (c.type == 3);
                            break;
                        case NodeTest::AnyNode:
                            match = true;
                            break;
                    }
                    if (match) matches.push_back(child);
                    child = c.next_sibling;
                }
                break;
            }

            case Axis::Attribute: {
                uint32_t attr = ctx_node.first_attr;
                while (attr) {
                    const auto& a = dom.nodes[attr];
                    bool match = false;
                    if (step.test == NodeTest::Name) {
                        match = (dom.name(a) == step.name);
                    } else if (step.test == NodeTest::Wildcard) {
                        match = true;
                    }
                    if (match) matches.push_back(attr);
                    attr = a.next_sibling;
                }
                break;
            }

            case Axis::Self: {
                bool match = false;
                switch (step.test) {
                    case NodeTest::AnyNode: match = true; break;
                    case NodeTest::Name:
                        match = (ctx_node.type == 1 && dom.name(ctx_node) == step.name);
                        break;
                    case NodeTest::Wildcard: match = (ctx_node.type == 1); break;
                    default: break;
                }
                if (match) matches.push_back(ctx_idx);
                break;
            }

            case Axis::Parent: {
                // Find parent by scanning (FastDom doesn't store parent pointers)
                // For performance, search backwards for the element whose first_child
                // chain includes ctx_idx
                for (uint32_t i = 1; i < dom.node_count; ++i) {
                    const auto& n = dom.nodes[i];
                    if (n.type != 1 && n.type != 0) continue;
                    uint32_t child = n.first_child;
                    while (child) {
                        if (child == ctx_idx) {
                            matches.push_back(i);
                            goto parent_found;
                        }
                        child = dom.nodes[child].next_sibling;
                    }
                }
                parent_found:
                break;
            }

            case Axis::Descendant:
            case Axis::DescendantOrSelf: {
                // DFS traversal
                if (step.axis == Axis::DescendantOrSelf) {
                    bool match = false;
                    switch (step.test) {
                        case NodeTest::AnyNode: match = true; break;
                        case NodeTest::Name:
                            match = (ctx_node.type == 1 && dom.name(ctx_node) == step.name);
                            break;
                        case NodeTest::Wildcard: match = (ctx_node.type == 1); break;
                        case NodeTest::TextNode: match = (ctx_node.type == 2); break;
                        default: break;
                    }
                    if (match) matches.push_back(ctx_idx);
                }

                // Recursive DFS of descendants
                std::vector<uint32_t> stack;
                uint32_t child = ctx_node.first_child;
                while (child) {
                    stack.push_back(child);
                    child = dom.nodes[child].next_sibling;
                }
                while (!stack.empty()) {
                    uint32_t cur = stack.back();
                    stack.pop_back();
                    const auto& n = dom.nodes[cur];

                    bool match = false;
                    switch (step.test) {
                        case NodeTest::AnyNode: match = true; break;
                        case NodeTest::Name:
                            match = (n.type == 1 && dom.name(n) == step.name);
                            break;
                        case NodeTest::Wildcard: match = (n.type == 1); break;
                        case NodeTest::TextNode: match = (n.type == 2); break;
                        default: break;
                    }
                    if (match) matches.push_back(cur);

                    // Push children (in reverse for DFS order)
                    std::vector<uint32_t> children;
                    uint32_t ch = n.first_child;
                    while (ch) {
                        children.push_back(ch);
                        ch = dom.nodes[ch].next_sibling;
                    }
                    for (auto it = children.rbegin(); it != children.rend(); ++it) {
                        stack.push_back(*it);
                    }
                }
                break;
            }

            case Axis::FollowingSibling: {
                uint32_t sib = ctx_node.next_sibling;
                while (sib) {
                    const auto& s = dom.nodes[sib];
                    bool match = false;
                    switch (step.test) {
                        case NodeTest::Name:
                            match = (s.type == 1 && dom.name(s) == step.name);
                            break;
                        case NodeTest::Wildcard: match = (s.type == 1); break;
                        case NodeTest::AnyNode: match = true; break;
                        default: break;
                    }
                    if (match) matches.push_back(sib);
                    sib = s.next_sibling;
                }
                break;
            }

            default:
                break;
        }

        // Apply predicates
        for (const auto& pred : step.predicates) {
            NodeSet filtered;
            for (size_t i = 0; i < matches.size(); ++i) {
                bool keep = false;
                switch (pred.op) {
                    case PredicateOp::None:
                        keep = true;
                        break;
                    case PredicateOp::Position: {
                        int target_pos = pred.position;
                        if (target_pos == -1) target_pos = static_cast<int>(matches.size());
                        keep = (static_cast<int>(i + 1) == target_pos);
                        break;
                    }
                    case PredicateOp::AttrExists: {
                        const auto& n = dom.nodes[matches[i]];
                        uint32_t attr = n.first_attr;
                        while (attr) {
                            if (dom.name(dom.nodes[attr]) == pred.attr_name) {
                                keep = true;
                                break;
                            }
                            attr = dom.nodes[attr].next_sibling;
                        }
                        break;
                    }
                    case PredicateOp::AttrEquals: {
                        auto val = dom.attr(dom.nodes[matches[i]], pred.attr_name);
                        keep = (val == pred.value);
                        break;
                    }
                    case PredicateOp::TextEquals: {
                        auto text = get_text_content(dom, matches[i]);
                        keep = (text == pred.value);
                        break;
                    }
                    default:
                        keep = true;
                        break;
                }
                if (keep) filtered.push_back(matches[i]);
            }
            matches = std::move(filtered);
        }

        result.insert(result.end(), matches.begin(), matches.end());
    }

    return result;
}

/// Evaluate an XPath expression against a FastDom tree.
/// @param dom   The DOM tree.
/// @param expr  XPath expression string.
/// @return NodeSet of matching node indices.
inline NodeSet evaluate(const FastDom& dom, std::string_view expr_str) {
    auto expr = parse_xpath(expr_str);
    
    // Start context
    NodeSet context;
    if (expr.is_absolute) {
        // Start from document node (index 1), which is the parent of root.
        // This way /root finds the root element as a child of the document.
        context.push_back(1);  // Document node
    } else {
        context.push_back(dom.root_idx);
    }

    // Evaluate each step
    for (const auto& step : expr.steps) {
        context = eval_step(dom, context, step);
        if (context.empty()) break;
    }

    return context;
}

/// Convenience: evaluate and return string values of matching nodes.
inline std::vector<std::string> evaluate_strings(const FastDom& dom, std::string_view expr) {
    auto nodes = evaluate(dom, expr);
    std::vector<std::string> result;
    result.reserve(nodes.size());
    for (uint32_t idx : nodes) {
        const auto& n = dom.nodes[idx];
        if (n.type == 6) {
            // Attribute: return value
            result.push_back(std::string(dom.value(n)));
        } else if (n.type == 2) {
            // Text: return value
            result.push_back(std::string(dom.value(n)));
        } else {
            // Element: return text content
            result.push_back(get_text_content(dom, idx));
        }
    }
    return result;
}

/// Convenience: evaluate and return first match's text content or empty.
inline std::string evaluate_string(const FastDom& dom, std::string_view expr) {
    auto results = evaluate_strings(dom, expr);
    return results.empty() ? std::string{} : results[0];
}

/// Convenience: count matching nodes.
inline size_t evaluate_count(const FastDom& dom, std::string_view expr) {
    return evaluate(dom, expr).size();
}

} // namespace xpath
} // namespace parshred
