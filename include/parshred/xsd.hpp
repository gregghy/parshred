#pragma once
/// @file xsd.hpp
/// @brief XML Schema (XSD) simple type validation for parshred.
///
/// Provides validation of XML string values against built-in XSD simple types
/// and user-defined simple types with a subset of XSD facets. Also includes a
/// minimal XsdSchema class for validating a parshred::FastDom document against
/// declared element and attribute types.
///
/// This is a header-only library intended for correctness and graceful
/// handling of edge cases, not a full XSD 1.1 implementation.

#include <parshred/dom_fast.hpp>
#include <parshred/xpath.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace parshred {
namespace xsd {

// ── XSD Built-in Types ─────────────────────────────────────────────────

/// Built-in XSD simple types supported by this validator.
enum class XsdType : uint8_t {
    String,
    NormalizedString,
    Token,
    Boolean,
    Integer,
    NonNegativeInteger,
    PositiveInteger,
    NonPositiveInteger,
    NegativeInteger,
    Int,
    Short,
    Byte,
    Long,
    UnsignedInt,
    UnsignedShort,
    UnsignedByte,
    UnsignedLong,
    Float,
    Double,
    Decimal,
    Date,
    DateTime,
    Time,
    Duration,
    AnyURI,
    QName,
    NCName,
    Name,
    NMTOKEN,
    ID,
    IDREF,
    Base64Binary,
    HexBinary,
};

/// Whitespace facet values.
enum class WhitespaceHandling : uint8_t {
    Preserve,  ///< Keep whitespace unchanged.
    Replace,   ///< Replace \r, \n, \t with space.
    Collapse,  ///< Replace then collapse consecutive spaces and trim ends.
};

// ── Facets ─────────────────────────────────────────────────────────────

/// Restrictions that can be applied to an XSD simple type.
struct XsdFacet {
    std::optional<size_t> min_length;
    std::optional<size_t> max_length;
    std::optional<std::string> pattern;
    std::vector<std::string> enumeration;
    std::optional<std::string> min_inclusive;
    std::optional<std::string> max_inclusive;
    std::optional<std::string> min_exclusive;
    std::optional<std::string> max_exclusive;
    std::optional<int> total_digits;
    std::optional<int> fraction_digits;
    WhitespaceHandling whitespace_handling = WhitespaceHandling::Preserve;
};

/// A user-defined simple type: a base type optionally constrained by facets.
struct XsdSimpleType {
    XsdType base_type;
    XsdFacet facets;
    std::string name;
};

// ── Internal Helpers ─────────────────────────────────────────────────

namespace detail {

inline std::string_view trim(std::string_view s) noexcept {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

inline std::string replace_whitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == '\t') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

inline std::string collapse_whitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = true;  // leading spaces are skipped
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
        } else {
            out.push_back(c);
            in_space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline std::string apply_whitespace(std::string_view value, WhitespaceHandling handling) {
    switch (handling) {
        case WhitespaceHandling::Preserve:  return std::string(value);
        case WhitespaceHandling::Replace:   return replace_whitespace(value);
        case WhitespaceHandling::Collapse:  return collapse_whitespace(value);
    }
    return std::string(value);
}

inline bool is_name_start(char c) noexcept {
    // XML 1.0 name-start: letter, underscore, colon. Letters via ASCII + high bytes.
    unsigned char uc = static_cast<unsigned char>(c);
    if (c == '_' || c == ':') return true;
    if (uc >= 0x80) return true;  // accept UTF-8 bytes
    return (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z');
}

inline bool is_name_char(char c) noexcept {
    unsigned char uc = static_cast<unsigned char>(c);
    if (is_name_start(c)) return true;
    if (uc >= '0' && uc <= '9') return true;
    if (c == '-' || c == '.') return true;
    return false;
}

inline bool is_ncname_start(char c) noexcept {
    return is_name_start(c) && c != ':';
}

inline bool is_ncname_char(char c) noexcept {
    return is_name_char(c) && c != ':';
}

inline bool is_nmtoken_char(char c) noexcept {
    return is_name_char(c);
}

inline bool is_hex(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool is_base64(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

inline bool is_integer(std::string_view s) noexcept {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

inline bool parse_ll(std::string_view s, long long& out) noexcept {
    out = 0;
    if (!is_integer(s)) return false;
    std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

inline bool parse_ull(std::string_view s, unsigned long long& out) noexcept {
    out = 0;
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '+') ++i;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

inline bool parse_double(std::string_view s, double& out) noexcept {
    out = 0.0;
    try {
        out = std::stod(std::string(s));
    } catch (...) {
        return false;
    }
    return std::isfinite(out);
}

inline bool is_numeric_family(XsdType t) noexcept {
    switch (t) {
        case XsdType::Integer:
        case XsdType::NonNegativeInteger:
        case XsdType::PositiveInteger:
        case XsdType::NonPositiveInteger:
        case XsdType::NegativeInteger:
        case XsdType::Int:
        case XsdType::Short:
        case XsdType::Byte:
        case XsdType::Long:
        case XsdType::UnsignedInt:
        case XsdType::UnsignedShort:
        case XsdType::UnsignedByte:
        case XsdType::UnsignedLong:
        case XsdType::Float:
        case XsdType::Double:
        case XsdType::Decimal:
            return true;
        default:
            return false;
    }
}

} // namespace detail

// ── Built-in Type Validation ───────────────────────────────────────────

/// Validates a string value against a built-in XSD simple type.
inline bool validate_value(std::string_view value, XsdType type) {
    using namespace detail;
    const std::string_view s = trim(value);

    switch (type) {
        case XsdType::String:
            return true;  // any string is valid

        case XsdType::NormalizedString: {
            for (char c : value) {
                if (c == '\r' || c == '\n' || c == '\t') return false;
            }
            return true;
        }

        case XsdType::Token: {
            if (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) return false;
            if (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) return false;
            bool prev_space = false;
            for (char c : value) {
                if (c == '\r' || c == '\n' || c == '\t') return false;
                bool sp = std::isspace(static_cast<unsigned char>(c));
                if (sp && prev_space) return false;
                prev_space = sp;
            }
            return true;
        }

        case XsdType::Boolean:
            return s == "true" || s == "false" || s == "1" || s == "0";

        case XsdType::Integer:
            return is_integer(s);

        case XsdType::NonNegativeInteger: {
            if (s.empty() || s[0] == '-') return false;
            return is_integer(s);
        }

        case XsdType::PositiveInteger: {
            long long v;
            if (!parse_ll(s, v)) return false;
            // XSD integer lexical space forbids leading zeros.
            size_t i = 0;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i < s.size() && s[i] == '0') return false;
            return v > 0;
        }

        case XsdType::NonPositiveInteger: {
            long long v;
            if (!parse_ll(s, v)) return false;
            return v <= 0;
        }

        case XsdType::NegativeInteger: {
            long long v;
            if (!parse_ll(s, v)) return false;
            return v < 0;
        }

        case XsdType::Int: {
            long long v;
            if (!parse_ll(s, v)) return false;
            return v >= std::numeric_limits<int32_t>::min() &&
                   v <= std::numeric_limits<int32_t>::max();
        }

        case XsdType::Short: {
            long long v;
            if (!parse_ll(s, v)) return false;
            return v >= std::numeric_limits<int16_t>::min() &&
                   v <= std::numeric_limits<int16_t>::max();
        }

        case XsdType::Byte: {
            long long v;
            if (!parse_ll(s, v)) return false;
            return v >= std::numeric_limits<int8_t>::min() &&
                   v <= std::numeric_limits<int8_t>::max();
        }

        case XsdType::Long: {
            long long v;
            return parse_ll(s, v);
        }

        case XsdType::UnsignedInt: {
            unsigned long long v;
            if (!parse_ull(s, v)) return false;
            return v <= std::numeric_limits<uint32_t>::max();
        }

        case XsdType::UnsignedShort: {
            unsigned long long v;
            if (!parse_ull(s, v)) return false;
            return v <= std::numeric_limits<uint16_t>::max();
        }

        case XsdType::UnsignedByte: {
            unsigned long long v;
            if (!parse_ull(s, v)) return false;
            return v <= std::numeric_limits<uint8_t>::max();
        }

        case XsdType::UnsignedLong: {
            unsigned long long v;
            return parse_ull(s, v);
        }

        case XsdType::Decimal: {
            if (s.empty()) return false;
            static const std::regex dec_re(R"(^[+-]?(\d+(\.\d*)?|\.\d+)$)");
            return std::regex_match(std::string(s), dec_re);
        }

        case XsdType::Float:
        case XsdType::Double: {
            if (s == "INF" || s == "-INF" || s == "NaN") return true;
            static const std::regex fp_re(
                R"(^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$)");
            if (std::regex_match(std::string(s), fp_re)) return true;
            return false;
        }

        case XsdType::Date: {
            // YYYY-MM-DD with optional timezone
            static const std::regex date_re(
                R"(^\d{4}-\d{2}-\d{2}([+-]\d{2}:\d{2}|Z)?$)");
            return std::regex_match(std::string(s), date_re);
        }

        case XsdType::DateTime: {
            static const std::regex dt_re(
                R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?([+-]\d{2}:\d{2}|Z)?$)");
            return std::regex_match(std::string(s), dt_re);
        }

        case XsdType::Time: {
            static const std::regex time_re(
                R"(^\d{2}:\d{2}:\d{2}(\.\d+)?([+-]\d{2}:\d{2}|Z)?$)");
            return std::regex_match(std::string(s), time_re);
        }

        case XsdType::Duration: {
            // Basic ISO 8601 duration: P[nY][nM][nD][T[nH][nM][nS]]
            static const std::regex dur_re(
                R"(^P(\d+Y)?(\d+M)?(\d+D)?(T(\d+H)?(\d+M)?(\d+(\.\d+)?S)?)?$)");
            if (!std::regex_match(std::string(s), dur_re)) return false;
            // Must contain at least one component after P, and T if time part present.
            if (s.size() <= 1) return false;
            bool has_letter = false;
            for (size_t i = 1; i < s.size(); ++i) {
                char c = s[i];
                if (c == 'Y' || c == 'M' || c == 'D' || c == 'H' || c == 'S' ||
                    (c == 'T' && i > 1)) {
                    has_letter = true;
                    break;
                }
            }
            return has_letter;
        }

        case XsdType::AnyURI:
            return !s.empty();

        case XsdType::QName: {
            size_t colon = s.find(':');
            if (colon == std::string_view::npos) {
                return !s.empty() && is_ncname_start(s[0]) &&
                       std::all_of(s.begin(), s.end(), is_ncname_char);
            }
            std::string_view prefix = s.substr(0, colon);
            std::string_view local = s.substr(colon + 1);
            if (prefix.empty() || local.empty()) return false;
            if (s.find(':', colon + 1) != std::string_view::npos) return false;
            return is_ncname_start(prefix[0]) &&
                   std::all_of(prefix.begin(), prefix.end(), is_ncname_char) &&
                   is_ncname_start(local[0]) &&
                   std::all_of(local.begin(), local.end(), is_ncname_char);
        }

        case XsdType::NCName:
        case XsdType::ID:
        case XsdType::IDREF: {
            if (s.empty()) return false;
            return is_ncname_start(s[0]) &&
                   std::all_of(s.begin(), s.end(), is_ncname_char);
        }

        case XsdType::Name: {
            if (s.empty()) return false;
            return is_name_start(s[0]) &&
                   std::all_of(s.begin(), s.end(), is_name_char);
        }

        case XsdType::NMTOKEN: {
            if (s.empty()) return false;
            return std::all_of(s.begin(), s.end(), is_nmtoken_char);
        }

        case XsdType::Base64Binary: {
            if (s.empty()) return true;  // empty base64 is valid
            size_t eq = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                char c = s[i];
                if (!is_base64(c)) return false;
                if (c == '=') {
                    ++eq;
                    if (eq > 2) return false;
                    // padding can only appear at the end
                    for (size_t j = i + 1; j < s.size(); ++j) {
                        if (s[j] != '=') return false;
                    }
                }
            }
            return (s.size() % 4) == 0;
        }

        case XsdType::HexBinary: {
            if (s.size() % 2 != 0) return false;
            for (char c : s) {
                if (!is_hex(c)) return false;
            }
            return true;
        }
    }
    return false;
}

// ── Facet Validation ───────────────────────────────────────────────────

namespace detail {

inline bool check_numeric_range(std::string_view value, const XsdFacet& facets, XsdType base) {
    if (!is_numeric_family(base)) return true;

    // For integer-family types use integer comparison; otherwise use double.
    bool integer_family = false;
    switch (base) {
        case XsdType::Integer:
        case XsdType::NonNegativeInteger:
        case XsdType::PositiveInteger:
        case XsdType::NonPositiveInteger:
        case XsdType::NegativeInteger:
        case XsdType::Int:
        case XsdType::Short:
        case XsdType::Byte:
        case XsdType::Long:
        case XsdType::UnsignedInt:
        case XsdType::UnsignedShort:
        case XsdType::UnsignedByte:
        case XsdType::UnsignedLong:
            integer_family = true;
            break;
        default:
            break;
    }

    if (integer_family) {
        long long v;
        if (!parse_ll(value, v)) return false;
        auto check_bound = [&](std::optional<std::string> bound, bool inclusive, bool lower) -> bool {
            if (!bound) return true;
            long long b;
            if (!parse_ll(*bound, b)) return true;  // cannot parse facet -> skip
            if (inclusive) return lower ? (v >= b) : (v <= b);
            return lower ? (v > b) : (v < b);
        };
        if (!check_bound(facets.min_inclusive, true, true)) return false;
        if (!check_bound(facets.max_inclusive, true, false)) return false;
        if (!check_bound(facets.min_exclusive, false, true)) return false;
        if (!check_bound(facets.max_exclusive, false, false)) return false;
        return true;
    }

    double v;
    if (!parse_double(value, v)) return false;
    auto check_bound = [&](std::optional<std::string> bound, bool inclusive, bool lower) -> bool {
        if (!bound) return true;
        double b;
        if (!parse_double(*bound, b)) return true;
        if (inclusive) return lower ? (v >= b) : (v <= b);
        return lower ? (v > b) : (v < b);
    };
    if (!check_bound(facets.min_inclusive, true, true)) return false;
    if (!check_bound(facets.max_inclusive, true, false)) return false;
    if (!check_bound(facets.min_exclusive, false, true)) return false;
    if (!check_bound(facets.max_exclusive, false, false)) return false;
    return true;
}

inline bool check_length(const std::string& value, const XsdFacet& facets) {
    size_t len = value.size();
    if (facets.min_length && len < *facets.min_length) return false;
    if (facets.max_length && len > *facets.max_length) return false;
    return true;
}

inline bool check_pattern(const std::string& value, const XsdFacet& facets) {
    if (!facets.pattern) return true;
    try {
        std::regex re(*facets.pattern);
        return std::regex_match(value, re);
    } catch (...) {
        return false;
    }
}

inline bool check_enumeration(const std::string& value, const XsdFacet& facets) {
    if (facets.enumeration.empty()) return true;
    for (const auto& e : facets.enumeration) {
        if (e == value) return true;
    }
    return false;
}

inline bool check_total_digits(std::string_view value, const XsdFacet& facets) {
    if (!facets.total_digits) return true;
    int digits = 0;
    for (char c : value) {
        if (c >= '0' && c <= '9') ++digits;
    }
    return digits <= *facets.total_digits;
}

inline bool check_fraction_digits(std::string_view value, const XsdFacet& facets) {
    if (!facets.fraction_digits) return true;
    size_t dot = value.find('.');
    if (dot == std::string_view::npos) return true;
    int frac = static_cast<int>(value.size() - dot - 1);
    return frac <= *facets.fraction_digits;
}

} // namespace detail

/// Validates a string value against a custom simple type with facets applied.
inline bool validate_value(std::string_view value, const XsdSimpleType& type) {
    using namespace detail;

    // Apply whitespace handling first.
    std::string normalized = apply_whitespace(value, type.facets.whitespace_handling);

    // Length, enumeration, and pattern facets apply to the normalized value.
    if (!check_length(normalized, type.facets)) return false;
    if (!check_enumeration(normalized, type.facets)) return false;
    if (!check_pattern(normalized, type.facets)) return false;

    // Validate against the base built-in type.
    if (!validate_value(normalized, type.base_type)) return false;

    // Numeric range facets.
    if (!check_numeric_range(normalized, type.facets, type.base_type)) return false;

    // Digit facets for decimal/integer types.
    switch (type.base_type) {
        case XsdType::Decimal:
        case XsdType::Integer:
        case XsdType::NonNegativeInteger:
        case XsdType::PositiveInteger:
        case XsdType::NonPositiveInteger:
        case XsdType::NegativeInteger:
            if (!check_total_digits(normalized, type.facets)) return false;
            if (!check_fraction_digits(normalized, type.facets)) return false;
            break;
        default:
            break;
    }

    return true;
}

// ── Schema Document Validation ─────────────────────────────────────────

/// Minimal schema definition for validating a FastDom document.
class XsdSchema {
public:
    /// Declares that elements with `name` expect the given built-in type.
    void add_element(std::string name, XsdType type) {
        elements_[std::move(name)] = XsdSimpleType{.base_type = type, .facets = {}, .name = ""};
    }

    /// Declares that elements with `name` expect the given custom simple type.
    void add_element(std::string name, XsdSimpleType type) {
        elements_[std::move(name)] = std::move(type);
    }

    /// Declares an attribute for elements named `element`.
    void add_attribute(std::string element, std::string attr, XsdType type, bool required = false) {
        attributes_[std::move(element)][std::move(attr)] = {type, required};
    }

    /// Validates a DOM against this schema. Returns a list of error messages.
    std::vector<std::string> validate_document(const parshred::FastDom& dom) const {
        std::vector<std::string> errors;
        if (!dom.nodes || dom.root_idx == 0) {
            errors.push_back("Document is empty");
            return errors;
        }
        validate_node(dom, dom.root_idx, errors);
        return errors;
    }

private:
    struct AttrDecl {
        XsdType type;
        bool required = false;
    };

    std::map<std::string, XsdSimpleType, std::less<>> elements_;
    std::map<std::string, std::map<std::string, AttrDecl, std::less<>>, std::less<>> attributes_;

    void validate_node(const parshred::FastDom& dom, uint32_t node_idx,
                       std::vector<std::string>& errors) const {
        const auto& node = dom.nodes[node_idx];
        if (node.type != 1) return;  // only validate element nodes

        std::string_view elem_name = dom.name(node);

        // Validate element text content if the element is declared.
        auto elem_it = elements_.find(elem_name);
        if (elem_it != elements_.end()) {
            std::string text = xpath::get_text_content(dom, node_idx);
            if (!validate_value(text, elem_it->second)) {
                errors.push_back(std::string("Element '") + std::string(elem_name) +
                                 "' has invalid value '" + text + "'");
            }
        }

        // Validate declared attributes.
        auto attr_map_it = attributes_.find(elem_name);
        if (attr_map_it != attributes_.end()) {
            std::set<std::string_view, std::less<>> present;
            uint32_t attr_idx = node.first_attr;
            while (attr_idx) {
                const auto& attr = dom.nodes[attr_idx];
                present.insert(dom.name(attr));
                attr_idx = attr.next_sibling;
            }

            for (const auto& [attr_name, decl] : attr_map_it->second) {
                auto present_it = present.find(attr_name);
                if (present_it == present.end()) {
                    if (decl.required) {
                        errors.push_back(std::string("Element '") + std::string(elem_name) +
                                         "' is missing required attribute '" + attr_name + "'");
                    }
                    continue;
                }
                std::string_view attr_value = dom.attr(node, attr_name);
                if (!validate_value(attr_value, decl.type)) {
                    errors.push_back(std::string("Element '") + std::string(elem_name) +
                                     "' attribute '" + attr_name + "' has invalid value '" +
                                     std::string(attr_value) + "'");
                }
            }
        }

        // Recurse into child elements.
        uint32_t child = node.first_child;
        while (child) {
            validate_node(dom, child, errors);
            child = dom.nodes[child].next_sibling;
        }
    }
};

} // namespace xsd
} // namespace parshred
