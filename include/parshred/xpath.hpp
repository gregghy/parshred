#pragma once
/// @file xpath.hpp
/// @brief XPath 1.0 evaluation engine for parshred DOM.
///
/// Supports a practical subset of XPath 1.0:
///   Location paths: absolute (/a/b), relative (a/b), descendant (//a)
///   Axes: child (default), attribute (@), self (.), parent (..)
///   Wildcards: *, @*
///   Predicates: [n], [@attr], [@attr='val'], [text()='val']
///   Functions (node): text(), name(), local-name(), namespace-uri(),
///                     count(), last(), position(),
///                     contains(), starts-with()
///   Functions (string): substring(), substring-before(), substring-after(),
///                       concat(), normalize-space(), translate(), string-length()
///   Functions (number): number(), sum(), floor(), ceiling(), round()
///   Functions (boolean): true(), false(), not()
///   Arithmetic operators: +, -, *, div, mod
///   Comparison operators: =, !=, <, <=, >, >=
///   Boolean operators: and, or
///   Union operator: |
///
/// Works with the FastDom structure from dom_fast.hpp.

#include <parshred/dom_fast.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
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
    [[nodiscard]] bool is_string()  const { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] bool is_number()  const { return std::holds_alternative<double>(data); }
    [[nodiscard]] bool is_boolean() const { return std::holds_alternative<bool>(data); }

    // Accessors
    [[nodiscard]] const NodeSet&    as_nodeset() const { return std::get<NodeSet>(data); }
    [[nodiscard]] NodeSet&          as_nodeset()       { return std::get<NodeSet>(data); }
    [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(data); }
    [[nodiscard]] double            as_number()  const { return std::get<double>(data); }
    [[nodiscard]] bool              as_boolean() const { return std::get<bool>(data); }

    // String conversion (XPath string() function semantics)
    [[nodiscard]] std::string to_string(const FastDom& dom) const {
        if (is_string()) return as_string();
        if (is_number()) {
            double v = as_number();
            if (std::isnan(v)) return "NaN";
            if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
            if (v == static_cast<int64_t>(v)) return std::to_string(static_cast<int64_t>(v));
            return std::to_string(v);
        }
        if (is_boolean()) return as_boolean() ? "true" : "false";
        if (is_nodeset() && !as_nodeset().empty()) {
            // String value of first node
            const auto& n = dom.nodes[as_nodeset()[0]];
            if (n.type == 6 || n.type == 2) return std::string(dom.value(n));
            // Element: return text content (DFS, document order)
            std::string out;
            std::vector<uint32_t> stack;
            {
                std::vector<uint32_t> children;
                uint32_t ch = n.first_child;
                while (ch) { children.push_back(ch); ch = dom.nodes[ch].next_sibling; }
                for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
            }
            while (!stack.empty()) {
                uint32_t cur = stack.back(); stack.pop_back();
                const auto& cn = dom.nodes[cur];
                if (cn.type == 2) out += std::string(dom.value(cn));
                else if (cn.type == 1) {
                    std::vector<uint32_t> kids;
                    uint32_t k = cn.first_child;
                    while (k) { kids.push_back(k); k = dom.nodes[k].next_sibling; }
                    for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
                }
            }
            return out;
        }
        return {};
    }

    // Number conversion (XPath number() semantics)
    [[nodiscard]] double to_number(const FastDom& dom) const {
        if (is_number()) return as_number();
        if (is_boolean()) return as_boolean() ? 1.0 : 0.0;
        std::string s = to_string(dom);
        // trim
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::numeric_limits<double>::quiet_NaN();
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
        try { return std::stod(s); } catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
    }

    // Boolean conversion (XPath boolean() semantics)
    [[nodiscard]] bool to_boolean() const {
        if (is_boolean()) return as_boolean();
        if (is_number()) { double v = as_number(); return v != 0.0 && !std::isnan(v); }
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

// ── Predicate Expression Tree ────────────────────────────────────────
// Forward declaration for recursive structure
struct XPathPredExpr;
using PredExprPtr = std::shared_ptr<XPathPredExpr>;

enum class PredExprKind : uint8_t {
    NumberLiteral,      // 3.14
    StringLiteral,      // 'hello'
    FuncCall,           // contains(@id, 'x')
    BinaryOp,           // left OP right
    UnaryMinus,         // -expr
    NotOp,              // not(expr)   (also handled as FuncCall, but kept separate)
    AttrRef,            // @name  — reads attribute value as string
    TextRef,            // text() — reads text content
    SelfNameRef,        // name() / local-name() of context node
    PositionRef,        // position()
    LastRef,            // last()
    PathExpr,           // a location path evaluated relative to context node
                        //   (stored as string and parsed lazily, or as steps vector)
};

enum class BinOp : uint8_t {
    // Arithmetic
    Add, Sub, Mul, Div, Mod,
    // Comparison
    Eq, NEq, Lt, Lte, Gt, Gte,
    // Boolean
    And, Or,
};

struct XPathPredExpr {
    PredExprKind kind;

    // For literals
    double       num_val   = 0.0;
    std::string  str_val;

    // For function calls / binary ops
    std::string              func_name;
    std::vector<PredExprPtr> args;       // func args OR [left, right] for BinOp

    BinOp bin_op = BinOp::Add;          // used when kind == BinaryOp

    // For PathExpr: the sub-path string (e.g. "price", "@id", "title/text()")
    std::string  path_str;

    // Constructors
    static PredExprPtr make_num(double v) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::NumberLiteral;
        e->num_val = v;
        return e;
    }
    static PredExprPtr make_str(std::string s) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::StringLiteral;
        e->str_val = std::move(s);
        return e;
    }
    static PredExprPtr make_func(std::string name, std::vector<PredExprPtr> a) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::FuncCall;
        e->func_name = std::move(name);
        e->args = std::move(a);
        return e;
    }
    static PredExprPtr make_binop(BinOp op, PredExprPtr l, PredExprPtr r) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::BinaryOp;
        e->bin_op = op;
        e->args = {std::move(l), std::move(r)};
        return e;
    }
    static PredExprPtr make_unary_minus(PredExprPtr inner) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::UnaryMinus;
        e->args = {std::move(inner)};
        return e;
    }
    static PredExprPtr make_attr(std::string attr_name) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::AttrRef;
        e->str_val = std::move(attr_name);
        return e;
    }
    static PredExprPtr make_text() {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::TextRef;
        return e;
    }
    static PredExprPtr make_position() {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::PositionRef;
        return e;
    }
    static PredExprPtr make_last() {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::LastRef;
        return e;
    }
    static PredExprPtr make_path(std::string p) {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::PathExpr;
        e->path_str = std::move(p);
        return e;
    }
    static PredExprPtr make_selfname() {
        auto e = std::make_shared<XPathPredExpr>();
        e->kind = PredExprKind::SelfNameRef;
        return e;
    }
};

// Legacy predicate structs kept for backward compatibility
// (the new expression-tree system supersedes them but we map old ops to new)
enum class PredicateOp : uint8_t {
    None,
    Position,       // [n] — numeric position
    AttrExists,     // [@attr] — attribute exists
    AttrEquals,     // [@attr='value']
    TextEquals,     // [text()='value']
    FuncCall,       // [contains(@attr, 'val')], etc.
    Expr,           // Full expression tree (new)
};

struct Predicate {
    PredicateOp op = PredicateOp::Expr;
    std::string attr_name;
    std::string value;
    int position = 0;
    PredExprPtr expr;   // populated when op == Expr (or always in new parsing path)
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

// ── Helper: get text content of a node ──────────────────────────────

/// Get text content of a node (concatenation of all text children, recursive).
inline std::string get_text_content(const FastDom& dom, uint32_t node_idx) {
    const auto& node = dom.nodes[node_idx];
    if (node.type == 2) {
        return std::string(dom.value(node));
    }
    std::string result;
    // Iterative DFS preserving document order:
    // Push children in reverse so that the first child is on top of the stack.
    std::vector<uint32_t> stack;
    {
        // Collect children in order, then push in reverse
        std::vector<uint32_t> children;
        uint32_t child = node.first_child;
        while (child) { children.push_back(child); child = dom.nodes[child].next_sibling; }
        for (auto it = children.rbegin(); it != children.rend(); ++it) stack.push_back(*it);
    }
    while (!stack.empty()) {
        uint32_t cur = stack.back(); stack.pop_back();
        const auto& c = dom.nodes[cur];
        if (c.type == 2) {
            result += std::string(dom.value(c));
        } else if (c.type == 1) {
            std::vector<uint32_t> kids;
            uint32_t k = c.first_child;
            while (k) { kids.push_back(k); k = dom.nodes[k].next_sibling; }
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
        }
    }
    return result;
}

// ── Predicate Expression Parser ──────────────────────────────────────
// Forward declarations
struct PredParser {
    std::string_view src;
    size_t pos;

    explicit PredParser(std::string_view s, size_t start = 0)
        : src(s), pos(start) {}

    void skip_ws() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
               src[pos] == '\r' || src[pos] == '\n')) ++pos;
    }

    bool at_end() const { return pos >= src.size(); }

    char peek(size_t offset = 0) const {
        return (pos + offset < src.size()) ? src[pos + offset] : '\0';
    }

    bool starts_with(std::string_view sv) const {
        return src.substr(pos).starts_with(sv);
    }

    std::string read_name() {
        size_t start = pos;
        while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(src[pos])) ||
               src[pos] == '_' || src[pos] == '-' || src[pos] == ':')) {
            ++pos;
        }
        return std::string(src.substr(start, pos - start));
    }

    // Read a quoted string literal, advancing past the closing quote.
    std::string read_quoted() {
        char q = src[pos++];
        size_t start = pos;
        while (pos < src.size() && src[pos] != q) ++pos;
        std::string val(src.substr(start, pos - start));
        if (pos < src.size()) ++pos; // skip closing quote
        return val;
    }

    // Read a number literal (integer or decimal).
    double read_number() {
        size_t start = pos;
        if (pos < src.size() && src[pos] == '-') ++pos;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
        }
        try { return std::stod(std::string(src.substr(start, pos - start))); } catch (...) { return 0.0; }
    }

    // Parse a comma-separated list of expressions until ')'.
    std::vector<PredExprPtr> parse_arg_list() {
        std::vector<PredExprPtr> args;
        skip_ws();
        if (!at_end() && peek() == ')') return args;
        args.push_back(parse_or());
        while (!at_end() && peek() == ',') {
            ++pos; // skip ','
            skip_ws();
            args.push_back(parse_or());
        }
        return args;
    }

    // ── Grammar (recursive descent, operator precedence) ────────────
    // or-expr   := and-expr ('or' and-expr)*
    // and-expr  := cmp-expr ('and' cmp-expr)*
    // cmp-expr  := add-expr (('='|'!='|'<'|'<='|'>'|'>=') add-expr)?
    // add-expr  := mul-expr (('+' | '-') mul-expr)*
    // mul-expr  := unary (('*' | 'div' | 'mod') unary)*
    // unary     := '-' unary | primary
    // primary   := literal | func-call | '@'name | text() | position() |
    //              last() | name() | local-name() | namespace-uri() |
    //              number | path-expr | '(' or-expr ')'

    PredExprPtr parse_or() {
        auto left = parse_and();
        while (true) {
            skip_ws();
            if (starts_with("or") &&
                (pos + 2 >= src.size() || !std::isalnum(static_cast<unsigned char>(src[pos + 2])))) {
                pos += 2;
                skip_ws();
                auto right = parse_and();
                left = XPathPredExpr::make_binop(BinOp::Or, std::move(left), std::move(right));
            } else break;
        }
        return left;
    }

    PredExprPtr parse_and() {
        auto left = parse_cmp();
        while (true) {
            skip_ws();
            if (starts_with("and") &&
                (pos + 3 >= src.size() || !std::isalnum(static_cast<unsigned char>(src[pos + 3])))) {
                pos += 3;
                skip_ws();
                auto right = parse_cmp();
                left = XPathPredExpr::make_binop(BinOp::And, std::move(left), std::move(right));
            } else break;
        }
        return left;
    }

    PredExprPtr parse_cmp() {
        auto left = parse_add();
        skip_ws();
        if (!at_end()) {
            BinOp op;
            size_t advance = 0;
            char c0 = peek(0), c1 = peek(1);
            if      (c0 == '=' && c1 != '=')                 { op = BinOp::Eq;  advance = 1; }
            else if (c0 == '!' && c1 == '=')                 { op = BinOp::NEq; advance = 2; }
            else if (c0 == '<' && c1 == '=')                 { op = BinOp::Lte; advance = 2; }
            else if (c0 == '>' && c1 == '=')                 { op = BinOp::Gte; advance = 2; }
            else if (c0 == '<' && c1 != '<' && c1 != '/')   { op = BinOp::Lt;  advance = 1; }
            else if (c0 == '>' && c1 != '>' && c1 != '/')   { op = BinOp::Gt;  advance = 1; }
            else advance = 0;

            if (advance > 0) {
                pos += advance;
                skip_ws();
                auto right = parse_add();
                return XPathPredExpr::make_binop(op, std::move(left), std::move(right));
            }
        }
        return left;
    }

    PredExprPtr parse_add() {
        auto left = parse_mul();
        while (true) {
            skip_ws();
            if (!at_end() && peek() == '+') {
                ++pos; skip_ws();
                auto right = parse_mul();
                left = XPathPredExpr::make_binop(BinOp::Add, std::move(left), std::move(right));
            } else if (!at_end() && peek() == '-' &&
                       // make sure it's not part of a name
                       (pos + 1 < src.size() && src[pos + 1] != '-')) {
                ++pos; skip_ws();
                auto right = parse_mul();
                left = XPathPredExpr::make_binop(BinOp::Sub, std::move(left), std::move(right));
            } else break;
        }
        return left;
    }

    PredExprPtr parse_mul() {
        auto left = parse_unary();
        while (true) {
            skip_ws();
            if (!at_end() && peek() == '*') {
                // '*' inside a predicate expression is always multiply —
                // path wildcards live in the step parser, not here.
                ++pos; skip_ws();
                auto right = parse_unary();
                left = XPathPredExpr::make_binop(BinOp::Mul, std::move(left), std::move(right));
            } else if (starts_with("div") &&
                       (pos + 3 >= src.size() || !std::isalnum(static_cast<unsigned char>(src[pos + 3])))) {
                pos += 3; skip_ws();
                auto right = parse_unary();
                left = XPathPredExpr::make_binop(BinOp::Div, std::move(left), std::move(right));
            } else if (starts_with("mod") &&
                       (pos + 3 >= src.size() || !std::isalnum(static_cast<unsigned char>(src[pos + 3])))) {
                pos += 3; skip_ws();
                auto right = parse_unary();
                left = XPathPredExpr::make_binop(BinOp::Mod, std::move(left), std::move(right));
            } else break;
        }
        return left;
    }

    PredExprPtr parse_unary() {
        skip_ws();
        if (!at_end() && peek() == '-') {
            ++pos;
            skip_ws();
            auto inner = parse_primary();
            return XPathPredExpr::make_unary_minus(std::move(inner));
        }
        return parse_primary();
    }

    PredExprPtr parse_primary() {
        skip_ws();
        if (at_end()) return XPathPredExpr::make_num(0.0);

        char c = peek();

        // Grouped expression
        if (c == '(') {
            ++pos;
            auto e = parse_or();
            skip_ws();
            if (!at_end() && peek() == ')') ++pos;
            return e;
        }

        // String literal
        if (c == '\'' || c == '"') {
            return XPathPredExpr::make_str(read_quoted());
        }

        // Number literal (starts with digit)
        if (std::isdigit(static_cast<unsigned char>(c))) {
            return XPathPredExpr::make_num(read_number());
        }

        // @attr reference
        if (c == '@') {
            ++pos;
            std::string aname;
            if (!at_end() && peek() == '*') { ++pos; aname = "*"; }
            else aname = read_name();
            return XPathPredExpr::make_attr(std::move(aname));
        }

        // Absolute or descendant path starting with /
        if (c == '/') {
            std::string path;
            path += '/'; ++pos;
            if (!at_end() && peek() == '/') { path += '/'; ++pos; }
            // Now read the rest of the path step by step
            while (!at_end() && peek() != ')' && peek() != ']' && peek() != ',') {
                skip_ws();
                if (at_end()) break;
                // Attribute step
                if (peek() == '@') { path += '@'; ++pos; }
                // Node test or element name
                if (!at_end() && peek() == '*') {
                    path += '*'; ++pos;
                } else {
                    std::string nm2 = read_name();
                    if (nm2.empty()) break;
                    path += nm2;
                    // function-like: text(), node()
                    if (!at_end() && peek() == '(') {
                        path += '('; ++pos;
                        if (!at_end() && peek() == ')') { path += ')'; ++pos; }
                    }
                }
                skip_ws();
                // Optional predicate on this step
                if (!at_end() && peek() == '[') {
                    int d2 = 0;
                    while (!at_end()) {
                        char ch2 = src[pos++]; path += ch2;
                        if (ch2 == '[') ++d2;
                        else if (ch2 == ']') { --d2; if (d2 == 0) break; }
                    }
                }
                skip_ws();
                // Continue with next step?
                if (!at_end() && peek() == '/') {
                    path += '/'; ++pos;
                    if (!at_end() && peek() == '/') { path += '/'; ++pos; }
                } else break;
            }
            return XPathPredExpr::make_path(path);
        }

        // Keyword / function names
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t name_start = pos;
            std::string nm = read_name();
            skip_ws();

            // Check for function call: name followed by '('
            if (!at_end() && peek() == '(') {
                ++pos; // skip '('
                auto args = parse_arg_list();
                skip_ws();
                if (!at_end() && peek() == ')') ++pos;
                return XPathPredExpr::make_func(nm, std::move(args));
            }

            // Keywords that are operators (but weren't consumed by upper levels):
            // 'div', 'mod', 'and', 'or' without parens would be path names here.
            // Non-keyword bare names are path references.
            // The name could be a relative path step like "price" or "title/text()"
            // Collect the full sub-path including any / separators.
            // We stop at ] | ) and whitespace-followed-by-operator.
            (void)name_start; // suppress unused-variable warning
            std::string path = nm;
            // Continue if we see '/' (sub-path)
            while (!at_end() && (peek() == '/' || peek() == '[')) {
                if (peek() == '/') {
                    path += '/';
                    ++pos;
                    if (!at_end() && peek() == '/') { path += '/'; ++pos; }
                    // read next step
                    if (!at_end() && peek() == '@') { path += '@'; ++pos; }
                    std::string next_name = read_name();
                    if (next_name.empty()) {
                        // might be wildcard or text()
                        if (!at_end() && peek() == '*') { path += '*'; ++pos; }
                    } else {
                        path += next_name;
                        // handle text() etc.
                        if (!at_end() && peek() == '(') {
                            path += '(';
                            ++pos;
                            if (!at_end() && peek() == ')') { path += ')'; ++pos; }
                        }
                    }
                } else if (peek() == '[') {
                    // Predicate inside sub-path — read until matching ']'
                    int depth = 0;
                    while (!at_end()) {
                        char ch = src[pos++];
                        path += ch;
                        if (ch == '[') ++depth;
                        else if (ch == ']') { --depth; if (depth == 0) break; }
                    }
                }
            }
            return XPathPredExpr::make_path(path);
        }

        // dot: self or relative path
        if (c == '.') {
            // Could be .5 (decimal), or . (self), or .. (parent)
            if (pos + 1 < src.size() && src[pos + 1] == '.') {
                pos += 2;
                return XPathPredExpr::make_path("..");
            }
            if (pos + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[pos + 1]))) {
                return XPathPredExpr::make_num(read_number());
            }
            ++pos;
            return XPathPredExpr::make_path(".");
        }

        // Fallback: return empty string literal
        return XPathPredExpr::make_str("");
    }
};

// ── XPath Parser ─────────────────────────────────────────────────────

/// Parse an XPath expression string into an AST.
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

    // Parse predicate expression tree starting at '[', ending after ']'
    auto parse_predicate = [&]() -> Predicate {
        Predicate pred;
        ++pos; // skip '['
        skip_ws();

        // Find the matching ']' for the predicate body
        size_t body_start = pos;
        int depth = 1;
        size_t body_end = pos;
        while (body_end < expr.size() && depth > 0) {
            char ch = expr[body_end];
            if (ch == '[') ++depth;
            else if (ch == ']') --depth;
            else if ((ch == '\'' || ch == '"')) {
                char q = ch;
                ++body_end;
                while (body_end < expr.size() && expr[body_end] != q) ++body_end;
            }
            if (depth > 0) ++body_end;
        }
        std::string_view body = expr.substr(body_start, body_end - body_start);
        pos = body_end + 1; // skip past ']'

        // Parse body as expression
        PredParser pp(body, 0);
        pred.op = PredicateOp::Expr;
        pred.expr = pp.parse_or();

        // Fast-path: if the expression is a pure number literal, treat as position predicate
        // (maintains backward compatibility for [1], [2], etc.)
        // We still use the expression tree to evaluate.

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

        // Stop on union operator at top level — handled in evaluate()
        if (expr[pos] == '|') break;

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
        // Regular name (possibly with axis:: prefix)
        else {
            // Read the name
            std::string nm = read_name();
            if (nm.empty()) break;

            // Check for axis:: prefix
            if (pos < expr.size() && expr[pos] == ':' &&
                pos + 1 < expr.size() && expr[pos + 1] == ':') {
                pos += 2; // skip '::'
                Axis ax = Axis::Child;
                if      (nm == "child")              ax = Axis::Child;
                else if (nm == "descendant")         ax = Axis::Descendant;
                else if (nm == "descendant-or-self") ax = Axis::DescendantOrSelf;
                else if (nm == "parent")             ax = Axis::Parent;
                else if (nm == "self")               ax = Axis::Self;
                else if (nm == "attribute")          ax = Axis::Attribute;
                else if (nm == "ancestor")           ax = Axis::Ancestor;
                else if (nm == "ancestor-or-self")   ax = Axis::AncestorOrSelf;
                else if (nm == "following-sibling")  ax = Axis::FollowingSibling;
                else if (nm == "preceding-sibling")  ax = Axis::PrecedingSibling;
                step.axis = ax;

                // Now parse the node test after ::
                if (pos + 6 <= expr.size() && expr.substr(pos, 6) == "text()") {
                    step.test = NodeTest::TextNode; pos += 6;
                } else if (pos + 6 <= expr.size() && expr.substr(pos, 6) == "node()") {
                    step.test = NodeTest::AnyNode; pos += 6;
                } else if (pos < expr.size() && expr[pos] == '*') {
                    step.test = NodeTest::Wildcard; ++pos;
                } else {
                    step.test = NodeTest::Name;
                    step.name = read_name();
                }
            } else {
                step.test = NodeTest::Name;
                step.name = std::move(nm);
            }
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

// ── Predicate Expression Evaluator ───────────────────────────────────
// Forward-declare eval_xpath_internal (defined after eval_step)
inline NodeSet eval_xpath_internal(const FastDom& dom, const NodeSet& context, std::string_view path_str);

inline XPathValue eval_pred_expr(
    const FastDom& dom,
    const XPathPredExpr& expr,
    uint32_t node_idx,
    size_t   position,     // 1-based position in current match set
    size_t   last          // size of current match set
) {
    switch (expr.kind) {
        case PredExprKind::NumberLiteral:
            return XPathValue{expr.num_val};

        case PredExprKind::StringLiteral:
            return XPathValue{expr.str_val};

        case PredExprKind::AttrRef: {
            const auto& n = dom.nodes[node_idx];
            if (expr.str_val == "*") {
                // Existence: return boolean — any attribute exists
                return XPathValue{n.first_attr != 0};
            }
            auto val = dom.attr(n, expr.str_val);
            if (val.empty()) {
                // Check if attr really exists (could be empty string value)
                uint32_t a = n.first_attr;
                bool found = false;
                while (a) {
                    if (dom.name(dom.nodes[a]) == expr.str_val) { found = true; break; }
                    a = dom.nodes[a].next_sibling;
                }
                if (!found) {
                    // Return boolean false for existence predicates
                    return XPathValue{std::string{}};
                }
            }
            return XPathValue{std::string(val)};
        }

        case PredExprKind::TextRef:
            return XPathValue{get_text_content(dom, node_idx)};

        case PredExprKind::SelfNameRef: {
            const auto& n = dom.nodes[node_idx];
            return XPathValue{std::string(dom.name(n))};
        }

        case PredExprKind::PositionRef:
            return XPathValue{static_cast<double>(position)};

        case PredExprKind::LastRef:
            return XPathValue{static_cast<double>(last)};

        case PredExprKind::UnaryMinus: {
            auto v = eval_pred_expr(dom, *expr.args[0], node_idx, position, last);
            return XPathValue{-v.to_number(dom)};
        }

        case PredExprKind::NotOp: {
            auto v = eval_pred_expr(dom, *expr.args[0], node_idx, position, last);
            return XPathValue{!v.to_boolean()};
        }

        case PredExprKind::PathExpr: {
            // Evaluate the path relative to current node
            NodeSet ctx = {node_idx};
            auto ns = eval_xpath_internal(dom, ctx, expr.path_str);
            return XPathValue{std::move(ns)};
        }

        case PredExprKind::BinaryOp: {
            auto& lhs_expr = *expr.args[0];
            auto& rhs_expr = *expr.args[1];
            auto lv = eval_pred_expr(dom, lhs_expr, node_idx, position, last);
            auto rv = eval_pred_expr(dom, rhs_expr, node_idx, position, last);
            BinOp op = expr.bin_op;

            // Boolean short-circuit: and/or
            if (op == BinOp::And) return XPathValue{lv.to_boolean() && rv.to_boolean()};
            if (op == BinOp::Or)  return XPathValue{lv.to_boolean() || rv.to_boolean()};

            // Arithmetic
            if (op == BinOp::Add) return XPathValue{lv.to_number(dom) + rv.to_number(dom)};
            if (op == BinOp::Sub) return XPathValue{lv.to_number(dom) - rv.to_number(dom)};
            if (op == BinOp::Mul) return XPathValue{lv.to_number(dom) * rv.to_number(dom)};
            if (op == BinOp::Div) {
                double d = rv.to_number(dom);
                if (d == 0.0) return XPathValue{std::numeric_limits<double>::infinity()};
                return XPathValue{lv.to_number(dom) / d};
            }
            if (op == BinOp::Mod) {
                return XPathValue{std::fmod(lv.to_number(dom), rv.to_number(dom))};
            }

            // Comparison operators: XPath 1.0 — if both are node-sets, compare pairwise;
            // if one is a node-set vs string, compare string values; etc.
            // Simplified: compare as numbers if both look numeric, else as strings.
            auto do_compare = [&](const XPathValue& l, const XPathValue& r) -> bool {
                // If either is a nodeset, extract string value for comparison
                auto get_str = [&](const XPathValue& v) -> std::string {
                    return v.to_string(dom);
                };
                // Try numeric comparison first if both strings are numeric
                auto lstr = get_str(l);
                auto rstr = get_str(r);
                bool l_num = false, r_num = false;
                double ln = 0.0, rn = 0.0;
                try { ln = std::stod(lstr); l_num = true; } catch (...) {}
                try { rn = std::stod(rstr); r_num = true; } catch (...) {}

                // Also check if the XPathValue is explicitly a number
                if (l.is_number()) { ln = l.as_number(); l_num = true; }
                if (r.is_number()) { rn = r.as_number(); r_num = true; }

                bool both_num = l_num && r_num;

                switch (op) {
                    case BinOp::Eq:  return both_num ? (ln == rn) : (lstr == rstr);
                    case BinOp::NEq: return both_num ? (ln != rn) : (lstr != rstr);
                    case BinOp::Lt:  return both_num ? (ln <  rn) : (lstr <  rstr);
                    case BinOp::Lte: return both_num ? (ln <= rn) : (lstr <= rstr);
                    case BinOp::Gt:  return both_num ? (ln >  rn) : (lstr >  rstr);
                    case BinOp::Gte: return both_num ? (ln >= rn) : (lstr >= rstr);
                    default: return false;
                }
            };

            // Node-set vs node-set or scalar: XPath says "true if any pair satisfies"
            if (lv.is_nodeset() && rv.is_nodeset()) {
                for (uint32_t li : lv.as_nodeset()) {
                    XPathValue ls{std::string(
                        dom.nodes[li].type == 6 || dom.nodes[li].type == 2
                            ? dom.value(dom.nodes[li])
                            : std::string_view(get_text_content(dom, li)))};
                    for (uint32_t ri : rv.as_nodeset()) {
                        XPathValue rs{std::string(
                            dom.nodes[ri].type == 6 || dom.nodes[ri].type == 2
                                ? dom.value(dom.nodes[ri])
                                : std::string_view(get_text_content(dom, ri)))};
                        if (do_compare(ls, rs)) return XPathValue{true};
                    }
                }
                return XPathValue{false};
            }
            if (lv.is_nodeset()) {
                for (uint32_t li : lv.as_nodeset()) {
                    XPathValue ls{std::string(
                        dom.nodes[li].type == 6 || dom.nodes[li].type == 2
                            ? dom.value(dom.nodes[li])
                            : std::string_view(get_text_content(dom, li)))};
                    if (do_compare(ls, rv)) return XPathValue{true};
                }
                return XPathValue{false};
            }
            if (rv.is_nodeset()) {
                for (uint32_t ri : rv.as_nodeset()) {
                    XPathValue rs{std::string(
                        dom.nodes[ri].type == 6 || dom.nodes[ri].type == 2
                            ? dom.value(dom.nodes[ri])
                            : std::string_view(get_text_content(dom, ri)))};
                    if (do_compare(lv, rs)) return XPathValue{true};
                }
                return XPathValue{false};
            }
            return XPathValue{do_compare(lv, rv)};
        }

        case PredExprKind::FuncCall: {
            const std::string& fn = expr.func_name;

            // Helper: evaluate arg i
            auto arg = [&](size_t i) -> XPathValue {
                if (i < expr.args.size())
                    return eval_pred_expr(dom, *expr.args[i], node_idx, position, last);
                return XPathValue{std::string{}};
            };
            auto arg_str = [&](size_t i) -> std::string {
                return arg(i).to_string(dom);
            };
            auto arg_num = [&](size_t i) -> double {
                return arg(i).to_number(dom);
            };
            auto arg_bool = [&](size_t i) -> bool {
                return arg(i).to_boolean();
            };
            auto arg_ns = [&](size_t i) -> NodeSet {
                auto v = arg(i);
                if (v.is_nodeset()) return v.as_nodeset();
                return {};
            };

            // ── Boolean functions ────────────────────────────────────
            if (fn == "true")  return XPathValue{true};
            if (fn == "false") return XPathValue{false};
            if (fn == "not")   return XPathValue{!arg_bool(0)};
            if (fn == "boolean") return XPathValue{arg_bool(0)};

            // ── Node functions ───────────────────────────────────────
            if (fn == "position") return XPathValue{static_cast<double>(position)};
            if (fn == "last")     return XPathValue{static_cast<double>(last)};

            if (fn == "count") {
                auto ns = arg_ns(0);
                return XPathValue{static_cast<double>(ns.size())};
            }

            if (fn == "name" || fn == "local-name") {
                auto sv = expr.args.empty()
                    ? dom.name(dom.nodes[node_idx])
                    : [&]() -> std::string_view {
                          auto ns = arg_ns(0);
                          return ns.empty() ? std::string_view{} : dom.name(dom.nodes[ns[0]]);
                      }();
                if (fn == "local-name") {
                    auto colon = sv.find(':');
                    if (colon != std::string_view::npos) sv = sv.substr(colon + 1);
                }
                return XPathValue{std::string(sv)};
            }

            if (fn == "namespace-uri") {
                if (expr.args.empty()) {
                    auto sv = dom.name(dom.nodes[node_idx]);
                    auto colon = sv.find(':');
                    // Without a namespace resolver we return the prefix
                    (void)colon;
                    return XPathValue{std::string{}};
                }
                return XPathValue{std::string{}};
            }

            if (fn == "string") {
                if (expr.args.empty()) return XPathValue{get_text_content(dom, node_idx)};
                return XPathValue{arg_str(0)};
            }

            // ── String functions ─────────────────────────────────────
            if (fn == "contains") {
                auto s = arg_str(0);
                auto sub = arg_str(1);
                return XPathValue{s.find(sub) != std::string::npos};
            }

            if (fn == "starts-with") {
                auto s = arg_str(0);
                auto pre = arg_str(1);
                return XPathValue{s.size() >= pre.size() && s.substr(0, pre.size()) == pre};
            }

            if (fn == "ends-with") {
                auto s = arg_str(0);
                auto suf = arg_str(1);
                return XPathValue{s.size() >= suf.size() &&
                                  s.substr(s.size() - suf.size()) == suf};
            }

            if (fn == "string-length") {
                std::string s = expr.args.empty() ? get_text_content(dom, node_idx) : arg_str(0);
                return XPathValue{static_cast<double>(s.size())};
            }

            if (fn == "substring") {
                auto s = arg_str(0);
                double start_d = arg_num(1);
                // XPath substring is 1-based; round per spec
                long start = static_cast<long>(std::round(start_d)) - 1; // convert to 0-based
                if (start < 0) start = 0;
                if (static_cast<size_t>(start) >= s.size()) return XPathValue{std::string{}};
                if (expr.args.size() >= 3) {
                    double len_d = arg_num(2);
                    long len = static_cast<long>(std::round(len_d));
                    if (len <= 0) return XPathValue{std::string{}};
                    return XPathValue{s.substr(static_cast<size_t>(start),
                                               static_cast<size_t>(len))};
                }
                return XPathValue{s.substr(static_cast<size_t>(start))};
            }

            if (fn == "substring-before") {
                auto s = arg_str(0);
                auto needle = arg_str(1);
                auto p = s.find(needle);
                if (p == std::string::npos) return XPathValue{std::string{}};
                return XPathValue{s.substr(0, p)};
            }

            if (fn == "substring-after") {
                auto s = arg_str(0);
                auto needle = arg_str(1);
                auto p = s.find(needle);
                if (p == std::string::npos) return XPathValue{std::string{}};
                return XPathValue{s.substr(p + needle.size())};
            }

            if (fn == "concat") {
                std::string result;
                for (size_t i = 0; i < expr.args.size(); ++i) result += arg_str(i);
                return XPathValue{result};
            }

            if (fn == "normalize-space") {
                std::string s = expr.args.empty() ? get_text_content(dom, node_idx) : arg_str(0);
                std::string out;
                bool in_ws = true; // treat leading whitespace as collapsible
                for (char ch : s) {
                    bool ws = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');
                    if (ws) {
                        if (!in_ws && !out.empty()) out += ' ';
                        in_ws = true;
                    } else {
                        out += ch;
                        in_ws = false;
                    }
                }
                // trim trailing space
                if (!out.empty() && out.back() == ' ') out.pop_back();
                return XPathValue{out};
            }

            if (fn == "translate") {
                auto s    = arg_str(0);
                auto from = arg_str(1);
                auto to   = arg_str(2);
                std::string out;
                for (char ch : s) {
                    auto p = from.find(ch);
                    if (p == std::string::npos) {
                        out += ch;
                    } else if (p < to.size()) {
                        out += to[p];
                    }
                    // else: character is in 'from' but not in 'to' → drop it
                }
                return XPathValue{out};
            }

            // ── Number functions ─────────────────────────────────────
            if (fn == "number") {
                if (expr.args.empty()) return XPathValue{static_cast<double>(std::stod(get_text_content(dom, node_idx)))};
                return XPathValue{arg_num(0)};
            }

            if (fn == "sum") {
                auto ns = arg_ns(0);
                double total = 0.0;
                for (uint32_t idx : ns) {
                    std::string txt = get_text_content(dom, idx);
                    try { total += std::stod(txt); } catch (...) {}
                }
                return XPathValue{total};
            }

            if (fn == "floor")   return XPathValue{std::floor(arg_num(0))};
            if (fn == "ceiling") return XPathValue{std::ceil(arg_num(0))};
            if (fn == "round") {
                double v = arg_num(0);
                // XPath round: round half up (towards positive infinity)
                return XPathValue{std::floor(v + 0.5)};
            }
            if (fn == "abs")     return XPathValue{std::abs(arg_num(0))};

            // ── text() as function in predicate ─────────────────────
            if (fn == "text") return XPathValue{get_text_content(dom, node_idx)};

            // Unknown function: return empty string
            return XPathValue{std::string{}};
        }

        default:
            return XPathValue{std::string{}};
    }
}

// ── XPath Evaluator ──────────────────────────────────────────────────

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

            case Axis::PrecedingSibling: {
                // Find parent and iterate siblings before ctx_idx
                uint32_t parent_idx = 0;
                for (uint32_t i = 1; i < dom.node_count; ++i) {
                    const auto& n = dom.nodes[i];
                    if (n.type != 1 && n.type != 0) continue;
                    uint32_t ch = n.first_child;
                    while (ch) {
                        if (ch == ctx_idx) { parent_idx = i; goto prec_parent_found; }
                        ch = dom.nodes[ch].next_sibling;
                    }
                }
                prec_parent_found:
                if (parent_idx) {
                    uint32_t sib = dom.nodes[parent_idx].first_child;
                    while (sib && sib != ctx_idx) {
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
                }
                break;
            }

            case Axis::Ancestor:
            case Axis::AncestorOrSelf: {
                if (step.axis == Axis::AncestorOrSelf) {
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
                }
                // Walk up to root finding ancestors
                uint32_t cur = ctx_idx;
                while (cur > 1) {
                    uint32_t par = 0;
                    for (uint32_t i = 1; i < dom.node_count; ++i) {
                        const auto& n = dom.nodes[i];
                        if (n.type != 1 && n.type != 0) continue;
                        uint32_t ch = n.first_child;
                        while (ch) {
                            if (ch == cur) { par = i; goto anc_parent_found; }
                            ch = dom.nodes[ch].next_sibling;
                        }
                    }
                    break;
                    anc_parent_found:
                    {
                        const auto& pn = dom.nodes[par];
                        bool match = false;
                        switch (step.test) {
                            case NodeTest::AnyNode: match = true; break;
                            case NodeTest::Name:
                                match = (pn.type == 1 && dom.name(pn) == step.name);
                                break;
                            case NodeTest::Wildcard: match = (pn.type == 1); break;
                            default: break;
                        }
                        if (match) matches.push_back(par);
                        cur = par;
                    }
                }
                break;
            }

            default:
                break;
        }

        // Apply predicates
        for (const auto& pred : step.predicates) {
            NodeSet filtered;
            size_t total = matches.size();
            for (size_t i = 0; i < matches.size(); ++i) {
                bool keep = false;

                if (pred.op == PredicateOp::Expr && pred.expr) {
                    auto val = eval_pred_expr(dom, *pred.expr, matches[i], i + 1, total);

                    // If the result is a pure number, treat it as a position predicate
                    // (XPath 1.0 spec: [3] is equivalent to [position()=3])
                    if (val.is_number()) {
                        keep = (static_cast<size_t>(val.as_number()) == i + 1);
                    } else {
                        keep = val.to_boolean();
                    }
                } else {
                    // Legacy path (shouldn't be reached with new parser, kept for safety)
                    switch (pred.op) {
                        case PredicateOp::None: keep = true; break;
                        case PredicateOp::Position: {
                            int target_pos = pred.position;
                            if (target_pos == -1) target_pos = static_cast<int>(total);
                            keep = (static_cast<int>(i + 1) == target_pos);
                            break;
                        }
                        case PredicateOp::AttrExists: {
                            const auto& n = dom.nodes[matches[i]];
                            uint32_t attr = n.first_attr;
                            while (attr) {
                                if (dom.name(dom.nodes[attr]) == pred.attr_name) { keep = true; break; }
                                attr = dom.nodes[attr].next_sibling;
                            }
                            break;
                        }
                        case PredicateOp::AttrEquals: {
                            auto v = dom.attr(dom.nodes[matches[i]], pred.attr_name);
                            keep = (v == pred.value);
                            break;
                        }
                        case PredicateOp::TextEquals: {
                            auto text = get_text_content(dom, matches[i]);
                            keep = (text == pred.value);
                            break;
                        }
                        default: keep = true; break;
                    }
                }
                if (keep) filtered.push_back(matches[i]);
            }
            matches = std::move(filtered);
        }

        result.insert(result.end(), matches.begin(), matches.end());
    }

    return result;
}

/// Internal: evaluate an XPath string against a given context node set.
/// Handles sub-paths used inside predicate PathExpr nodes.
inline NodeSet eval_xpath_internal(const FastDom& dom, const NodeSet& context, std::string_view path_str) {
    // Check for union at top level
    // We need to find '|' outside of brackets/quotes
    std::vector<std::string_view> parts;
    size_t start = 0;
    int depth = 0;
    bool in_quote = false;
    char quote_ch = '\0';
    for (size_t i = 0; i < path_str.size(); ++i) {
        char c = path_str[i];
        if (in_quote) {
            if (c == quote_ch) in_quote = false;
        } else if (c == '\'' || c == '"') {
            in_quote = true; quote_ch = c;
        } else if (c == '[') ++depth;
        else if (c == ']') --depth;
        else if (c == '|' && depth == 0) {
            parts.push_back(path_str.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(path_str.substr(start));

    if (parts.size() == 1) {
        auto expr = parse_xpath(path_str);
        NodeSet ctx = context;
        if (expr.is_absolute) {
            ctx = {1}; // document node
        }
        for (const auto& step : expr.steps) {
            ctx = eval_step(dom, ctx, step);
            if (ctx.empty()) break;
        }
        return ctx;
    }

    // Union: evaluate each part and merge (dedup preserving document order)
    NodeSet merged;
    for (auto part : parts) {
        // trim whitespace
        size_t a = part.find_first_not_of(" \t\r\n");
        size_t b = part.find_last_not_of(" \t\r\n");
        if (a == std::string_view::npos) continue;
        part = part.substr(a, b - a + 1);

        auto sub = eval_xpath_internal(dom, context, part);
        for (uint32_t idx : sub) {
            if (std::find(merged.begin(), merged.end(), idx) == merged.end())
                merged.push_back(idx);
        }
    }
    // Sort by document order (node index)
    std::sort(merged.begin(), merged.end());
    return merged;
}

/// Evaluate an XPath expression against a FastDom tree.
/// @param dom   The DOM tree.
/// @param expr  XPath expression string.
/// @return NodeSet of matching node indices.
inline NodeSet evaluate(const FastDom& dom, std::string_view expr_str) {
    // Start context
    NodeSet root_context = {dom.root_idx};
    return eval_xpath_internal(dom, root_context, expr_str);
}

/// Convenience: evaluate and return string values of matching nodes.
inline std::vector<std::string> evaluate_strings(const FastDom& dom, std::string_view expr) {
    auto nodes = evaluate(dom, expr);
    std::vector<std::string> result;
    result.reserve(nodes.size());
    for (uint32_t idx : nodes) {
        const auto& n = dom.nodes[idx];
        if (n.type == 6) {
            result.push_back(std::string(dom.value(n)));
        } else if (n.type == 2) {
            result.push_back(std::string(dom.value(n)));
        } else {
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
