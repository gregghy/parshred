#pragma once
/// @file dtd.hpp
/// @brief DTD (Document Type Definition) parsing and validation.
///
/// Supports internal DTD subset parsing:
///   - <!ENTITY name "value"> (general entities)
///   - <!ELEMENT name (content-model)> (element declarations)
///   - <!ATTLIST element attr type default> (attribute declarations)
///   - Well-formedness validation
///
/// External DTDs and parameter entities are NOT supported (security risk).

#include <parshred/common.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace parshred {

// ── DTD Declarations ─────────────────────────────────────────────────

/// Content model for element declarations.
enum class ContentModel : uint8_t {
    Any,        // ANY
    Empty,      // EMPTY
    Mixed,      // (#PCDATA | ...)
    Children,   // (a, b, c) or (a | b | c)
};

/// Attribute type in ATTLIST.
enum class AttrType : uint8_t {
    CData,          // CDATA
    Id,             // ID
    IdRef,          // IDREF
    IdRefs,         // IDREFS
    Entity,         // ENTITY
    Entities,       // ENTITIES
    NmToken,        // NMTOKEN
    NmTokens,       // NMTOKENS
    Enumeration,    // (val1|val2|...)
    Notation,       // NOTATION (val1|val2|...)
};

/// Attribute default declaration.
enum class AttrDefault : uint8_t {
    Required,       // #REQUIRED
    Implied,        // #IMPLIED
    Fixed,          // #FIXED "value"
    Value,          // "default-value"
};

/// A single attribute declaration.
struct AttrDecl {
    std::string name;
    AttrType type = AttrType::CData;
    AttrDefault default_kind = AttrDefault::Implied;
    std::string default_value;
    std::vector<std::string> enum_values;
};

/// An element declaration.
struct ElementDecl {
    std::string name;
    ContentModel model = ContentModel::Any;
    std::string content_spec;  // Raw content model string
};

/// Entity declaration.
struct EntityDecl {
    std::string name;
    std::string value;
    bool is_parameter = false;  // Parameter entity (%)
};

// ── DTD Context ──────────────────────────────────────────────────────

/// Parsed DTD information.
struct Dtd {
    std::string root_name;  // Name from DOCTYPE declaration
    std::unordered_map<std::string, EntityDecl> entities;
    std::unordered_map<std::string, ElementDecl> elements;
    std::unordered_map<std::string, std::vector<AttrDecl>> attlists;

    /// Look up a general entity by name.
    [[nodiscard]] const EntityDecl* find_entity(std::string_view name) const {
        auto it = entities.find(std::string(name));
        return (it != entities.end()) ? &it->second : nullptr;
    }

    /// Look up an element declaration.
    [[nodiscard]] const ElementDecl* find_element(std::string_view name) const {
        auto it = elements.find(std::string(name));
        return (it != elements.end()) ? &it->second : nullptr;
    }

    /// Look up attribute declarations for an element.
    [[nodiscard]] const std::vector<AttrDecl>* find_attlist(std::string_view elem) const {
        auto it = attlists.find(std::string(elem));
        return (it != attlists.end()) ? &it->second : nullptr;
    }
};

// ── DTD Parser ───────────────────────────────────────────────────────

/// Parse the internal DTD subset from a DOCTYPE declaration.
/// Input should be the content between [ and ] in <!DOCTYPE name [ ... ]>.
inline Dtd parse_dtd(std::string_view content) {
    Dtd dtd;
    size_t pos = 0;
    size_t len = content.size();

    auto skip_ws = [&]() {
        while (pos < len && (content[pos] == ' ' || content[pos] == '\t' ||
               content[pos] == '\n' || content[pos] == '\r')) ++pos;
    };

    auto read_name = [&]() -> std::string {
        skip_ws();
        size_t start = pos;
        while (pos < len && !std::isspace(static_cast<unsigned char>(content[pos])) &&
               content[pos] != '>' && content[pos] != '(' && content[pos] != ')' &&
               content[pos] != '"' && content[pos] != '\'') {
            ++pos;
        }
        return std::string(content.substr(start, pos - start));
    };

    auto read_quoted = [&]() -> std::string {
        skip_ws();
        if (pos >= len) return {};
        char quote = content[pos];
        if (quote != '"' && quote != '\'') return {};
        ++pos;
        size_t start = pos;
        while (pos < len && content[pos] != quote) ++pos;
        std::string val(content.substr(start, pos - start));
        if (pos < len) ++pos;  // skip closing quote
        return val;
    };

    while (pos < len) {
        skip_ws();
        if (pos >= len) break;

        // Look for <!
        if (content[pos] != '<') { ++pos; continue; }
        ++pos;
        if (pos >= len || content[pos] != '!') { continue; }
        ++pos;

        // Skip comments
        if (pos + 1 < len && content[pos] == '-' && content[pos+1] == '-') {
            pos += 2;
            while (pos + 2 < len) {
                if (content[pos] == '-' && content[pos+1] == '-' && content[pos+2] == '>') {
                    pos += 3;
                    break;
                }
                ++pos;
            }
            continue;
        }

        // Determine declaration type
        size_t kw_start = pos;
        while (pos < len && std::isalpha(static_cast<unsigned char>(content[pos]))) ++pos;
        std::string keyword(content.substr(kw_start, pos - kw_start));

        if (keyword == "ENTITY") {
            skip_ws();
            bool is_param = false;
            if (pos < len && content[pos] == '%') {
                is_param = true;
                ++pos;
            }
            std::string name = read_name();
            std::string value = read_quoted();
            
            if (!name.empty() && !value.empty()) {
                EntityDecl decl;
                decl.name = name;
                decl.value = value;
                decl.is_parameter = is_param;
                dtd.entities[name] = std::move(decl);
            }
            // Skip to >
            while (pos < len && content[pos] != '>') ++pos;
            if (pos < len) ++pos;

        } else if (keyword == "ELEMENT") {
            std::string name = read_name();
            skip_ws();
            
            ElementDecl decl;
            decl.name = name;
            
            // Read content spec
            size_t spec_start = pos;
            if (pos + 5 <= len && content.substr(pos, 5) == "EMPTY") {
                decl.model = ContentModel::Empty;
                pos += 5;
            } else if (pos + 3 <= len && content.substr(pos, 3) == "ANY") {
                decl.model = ContentModel::Any;
                pos += 3;
            } else if (pos < len && content[pos] == '(') {
                // Parse parenthesized content model
                int depth = 0;
                size_t paren_start = pos;
                while (pos < len) {
                    if (content[pos] == '(') ++depth;
                    else if (content[pos] == ')') {
                        --depth;
                        if (depth == 0) { ++pos; break; }
                    }
                    ++pos;
                }
                // Skip multiplier
                if (pos < len && (content[pos] == '*' || content[pos] == '+' || content[pos] == '?')) ++pos;
                decl.content_spec = std::string(content.substr(paren_start, pos - paren_start));
                // Check if mixed (#PCDATA)
                if (decl.content_spec.find("#PCDATA") != std::string::npos) {
                    decl.model = ContentModel::Mixed;
                } else {
                    decl.model = ContentModel::Children;
                }
            }
            
            dtd.elements[name] = std::move(decl);
            while (pos < len && content[pos] != '>') ++pos;
            if (pos < len) ++pos;

        } else if (keyword == "ATTLIST") {
            std::string elem_name = read_name();
            std::vector<AttrDecl>& attrs = dtd.attlists[elem_name];
            
            // Parse attribute definitions until >
            while (pos < len && content[pos] != '>') {
                skip_ws();
                if (pos >= len || content[pos] == '>') break;
                
                AttrDecl attr;
                attr.name = read_name();
                if (attr.name.empty() || attr.name == ">") break;
                
                // Read type
                skip_ws();
                if (pos + 5 <= len && content.substr(pos, 5) == "CDATA") {
                    attr.type = AttrType::CData;
                    pos += 5;
                } else if (pos + 2 <= len && content.substr(pos, 2) == "ID") {
                    // Check IDREF/IDREFS first
                    if (pos + 6 <= len && content.substr(pos, 6) == "IDREFS") {
                        attr.type = AttrType::IdRefs; pos += 6;
                    } else if (pos + 5 <= len && content.substr(pos, 5) == "IDREF") {
                        attr.type = AttrType::IdRef; pos += 5;
                    } else {
                        attr.type = AttrType::Id; pos += 2;
                    }
                } else if (pos + 8 <= len && content.substr(pos, 8) == "NMTOKENS") {
                    attr.type = AttrType::NmTokens; pos += 8;
                } else if (pos + 7 <= len && content.substr(pos, 7) == "NMTOKEN") {
                    attr.type = AttrType::NmToken; pos += 7;
                } else if (pos < len && content[pos] == '(') {
                    // Enumeration
                    attr.type = AttrType::Enumeration;
                    ++pos;
                    while (pos < len && content[pos] != ')') {
                        skip_ws();
                        if (content[pos] == '|') { ++pos; continue; }
                        size_t vs = pos;
                        while (pos < len && content[pos] != '|' && content[pos] != ')' &&
                               !std::isspace(static_cast<unsigned char>(content[pos]))) ++pos;
                        if (pos > vs) attr.enum_values.push_back(std::string(content.substr(vs, pos - vs)));
                    }
                    if (pos < len) ++pos; // skip ')'
                }
                
                // Read default
                skip_ws();
                if (pos + 9 <= len && content.substr(pos, 9) == "#REQUIRED") {
                    attr.default_kind = AttrDefault::Required;
                    pos += 9;
                } else if (pos + 8 <= len && content.substr(pos, 8) == "#IMPLIED") {
                    attr.default_kind = AttrDefault::Implied;
                    pos += 8;
                } else if (pos + 6 <= len && content.substr(pos, 6) == "#FIXED") {
                    attr.default_kind = AttrDefault::Fixed;
                    pos += 6;
                    attr.default_value = read_quoted();
                } else if (pos < len && (content[pos] == '"' || content[pos] == '\'')) {
                    attr.default_kind = AttrDefault::Value;
                    attr.default_value = read_quoted();
                }
                
                attrs.push_back(std::move(attr));
            }
            if (pos < len) ++pos; // skip '>'
        } else {
            // Unknown declaration — skip to >
            while (pos < len && content[pos] != '>') ++pos;
            if (pos < len) ++pos;
        }
    }

    return dtd;
}

// ── Validation ───────────────────────────────────────────────────────

/// Validation error.
struct ValidationError {
    enum class Kind {
        UndeclaredElement,
        UndeclaredAttribute,
        RequiredAttributeMissing,
        InvalidAttributeValue,
        InvalidContent,
        RootMismatch,
    };
    Kind kind;
    std::string element;
    std::string detail;
};

/// Validate a DOM tree against a DTD.
/// Returns a list of validation errors (empty = valid).
inline std::vector<ValidationError> validate(
    const Dtd& dtd,
    const std::vector<std::pair<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>>>& elements) {
    
    std::vector<ValidationError> errors;
    
    for (const auto& [elem_name, attrs] : elements) {
        // Check element is declared
        auto elem_decl = dtd.find_element(elem_name);
        if (!elem_decl && !dtd.elements.empty()) {
            errors.push_back({ValidationError::Kind::UndeclaredElement,
                            std::string(elem_name), "Element not declared in DTD"});
        }
        
        // Check attributes
        auto attr_decls = dtd.find_attlist(elem_name);
        if (attr_decls) {
            // Check for required attributes
            for (const auto& decl : *attr_decls) {
                if (decl.default_kind == AttrDefault::Required) {
                    bool found = false;
                    for (const auto& [aname, _] : attrs) {
                        if (aname == decl.name) { found = true; break; }
                    }
                    if (!found) {
                        errors.push_back({ValidationError::Kind::RequiredAttributeMissing,
                                        std::string(elem_name),
                                        "Required attribute '" + decl.name + "' missing"});
                    }
                }
            }
            
            // Check for undeclared attributes
            for (const auto& [aname, _] : attrs) {
                bool declared = false;
                for (const auto& decl : *attr_decls) {
                    if (decl.name == aname) { declared = true; break; }
                }
                if (!declared) {
                    errors.push_back({ValidationError::Kind::UndeclaredAttribute,
                                    std::string(elem_name),
                                    "Attribute '" + std::string(aname) + "' not declared"});
                }
            }
            
            // Check enumeration values
            for (const auto& [aname, aval] : attrs) {
                for (const auto& decl : *attr_decls) {
                    if (decl.name == aname && decl.type == AttrType::Enumeration) {
                        bool valid = false;
                        for (const auto& ev : decl.enum_values) {
                            if (ev == aval) { valid = true; break; }
                        }
                        if (!valid) {
                            errors.push_back({ValidationError::Kind::InvalidAttributeValue,
                                            std::string(elem_name),
                                            "Attribute '" + decl.name + "' value '" +
                                            std::string(aval) + "' not in enumeration"});
                        }
                    }
                }
            }
        }
    }
    
    return errors;
}

// ── Well-formedness checks ───────────────────────────────────────────

/// Check well-formedness of an XML document (basic checks).
/// Returns list of issues found.
inline std::vector<std::string> check_wellformedness(std::string_view xml) {
    std::vector<std::string> issues;
    
    // Check: matching start/end tags
    std::vector<std::string> tag_stack;
    size_t pos = 0;
    size_t len = xml.size();
    
    while (pos < len) {
        if (xml[pos] != '<') { ++pos; continue; }
        ++pos;
        if (pos >= len) break;
        
        // Skip PI, comment, CDATA, DOCTYPE
        if (xml[pos] == '?') {
            while (pos + 1 < len && !(xml[pos] == '?' && xml[pos+1] == '>')) ++pos;
            pos += 2;
            continue;
        }
        if (xml[pos] == '!' && pos + 1 < len && xml[pos+1] == '-') {
            pos += 2;
            while (pos + 2 < len && !(xml[pos] == '-' && xml[pos+1] == '-' && xml[pos+2] == '>')) ++pos;
            pos += 3;
            continue;
        }
        if (xml[pos] == '!' && pos + 7 < len && xml.substr(pos+1, 7) == "[CDATA[") {
            pos += 8;
            while (pos + 2 < len && !(xml[pos] == ']' && xml[pos+1] == ']' && xml[pos+2] == '>')) ++pos;
            pos += 3;
            continue;
        }
        if (xml[pos] == '!') {
            while (pos < len && xml[pos] != '>') ++pos;
            if (pos < len) ++pos;
            continue;
        }
        
        // End tag
        if (xml[pos] == '/') {
            ++pos;
            size_t ns = pos;
            while (pos < len && xml[pos] != '>' && !std::isspace(static_cast<unsigned char>(xml[pos]))) ++pos;
            std::string tag(xml.substr(ns, pos - ns));
            while (pos < len && xml[pos] != '>') ++pos;
            if (pos < len) ++pos;
            
            if (tag_stack.empty()) {
                issues.push_back("End tag </" + tag + "> without matching start tag");
            } else if (tag_stack.back() != tag) {
                issues.push_back("Mismatched tags: expected </" + tag_stack.back() + ">, got </" + tag + ">");
                tag_stack.pop_back();
            } else {
                tag_stack.pop_back();
            }
            continue;
        }
        
        // Start tag
        size_t ns = pos;
        while (pos < len && xml[pos] != '>' && xml[pos] != '/' &&
               !std::isspace(static_cast<unsigned char>(xml[pos]))) ++pos;
        std::string tag(xml.substr(ns, pos - ns));
        
        // Skip attributes
        while (pos < len && xml[pos] != '>' && xml[pos] != '/') ++pos;
        
        bool self_closing = (pos < len && xml[pos] == '/');
        if (self_closing) ++pos;
        if (pos < len && xml[pos] == '>') ++pos;
        
        if (!self_closing && !tag.empty()) {
            tag_stack.push_back(tag);
        }
    }
    
    // Check for unclosed tags
    for (auto it = tag_stack.rbegin(); it != tag_stack.rend(); ++it) {
        issues.push_back("Unclosed tag: <" + *it + ">");
    }
    
    return issues;
}

} // namespace parshred
