#pragma once
/// @file dom_parser.hpp
/// @brief Ultra-fast DOM parser — beats RapidXML at all sizes.
///
/// Two modes:
///   - In-situ: mutates input buffer (fastest possible, like RapidXML)
///   - Arena: read-only input, arena-allocated values for entities
///
/// The parse loop is integrated (not layered on SAX) — no per-element
/// function call overhead. Nodes are constructed directly in the hot loop.
///
/// Template flags control compile-time feature selection (like RapidXML):
///   dom_fastest:      no entities, no text nodes, no whitespace norm
///   dom_default:      entities expanded, text nodes created
///   dom_full:         all features including comments, PIs, doctype

#include <parshred/dom.hpp>
#include <parshred/dom_pool.hpp>
#include <parshred/lookup_tables.hpp>
#include <parshred/simd_utils.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace parshred {

// ── Parse flags (compile-time template parameters) ───────────────────

/// No entity translation, no text nodes, no whitespace normalization.
/// Maximum speed — equivalent to RapidXML's parse_fastest.
inline constexpr unsigned DOM_NO_ENTITIES      = 1u << 0;
inline constexpr unsigned DOM_NO_TEXT_NODES     = 1u << 1;
inline constexpr unsigned DOM_NO_COMMENTS       = 1u << 2;
inline constexpr unsigned DOM_NO_PIS            = 1u << 3;
inline constexpr unsigned DOM_NO_DOCTYPE        = 1u << 4;
inline constexpr unsigned DOM_NO_DECLARATIONS   = 1u << 5;
inline constexpr unsigned DOM_NO_DATA_NODES     = DOM_NO_TEXT_NODES | DOM_NO_COMMENTS | DOM_NO_PIS;
inline constexpr unsigned DOM_INSITU            = 1u << 6;  // Mutate input buffer

// Preset combinations
inline constexpr unsigned DOM_FASTEST = DOM_NO_ENTITIES | DOM_NO_DATA_NODES |
                                        DOM_NO_DOCTYPE | DOM_NO_DECLARATIONS | DOM_INSITU;
inline constexpr unsigned DOM_FAST    = DOM_NO_ENTITIES | DOM_NO_COMMENTS | DOM_INSITU;
inline constexpr unsigned DOM_DEFAULT = 0;  // Full parsing, read-only input
inline constexpr unsigned DOM_FULL    = 0;  // Same as default (all features enabled)

// ── DOM Parser ───────────────────────────────────────────────────────

/// Result of a DOM parse operation.
struct DomParseResult {
    XmlDocument doc;
    NodePool    pool;
    StringPool  strings;  // Only used in arena mode for expanded entities
};

/// Parse XML into a DOM tree.
///
/// @tparam Flags  Compile-time flags controlling which features are enabled.
/// @param data    Pointer to XML data (mutated if DOM_INSITU flag is set).
/// @param len     Length of data in bytes.
/// @return        DomParseResult containing the document, node pool, and string pool.
///
/// Usage:
/// @code
///   // Fastest possible (in-situ, no text nodes):
///   std::vector<char> buf(xml.begin(), xml.end());
///   buf.push_back('\0');
///   auto result = parshred::dom_parse<parshred::DOM_FASTEST>(buf.data(), buf.size() - 1);
///   auto* root = result.doc.root();
///
///   // Safe mode (read-only input, full features):
///   auto result = parshred::dom_parse<parshred::DOM_DEFAULT>(data, len);
/// @endcode
template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(char* data, size_t len);

/// Convenience: parse from const data (forces non-insitu mode).
template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(const char* data, size_t len) {
    static_assert(!(Flags & DOM_INSITU),
                  "Cannot use DOM_INSITU with const input. Use non-const overload.");
    return dom_parse<Flags>(const_cast<char*>(data), len);
}

/// Convenience: parse from string_view.
template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(std::string_view input) {
    static_assert(!(Flags & DOM_INSITU),
                  "Cannot use DOM_INSITU with string_view. Use char* overload.");
    return dom_parse<Flags>(const_cast<char*>(input.data()), input.size());
}

} // namespace parshred

// Include the template implementation
#include <parshred/dom_parser_impl.hpp>
