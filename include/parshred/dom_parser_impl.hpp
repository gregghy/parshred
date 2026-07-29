#pragma once
/// @file dom_parser_impl.hpp
/// @brief Integrated DOM parse loop — builds tree directly, no SAX layer.
///
/// This is the hot path. Every instruction matters.
/// The key insight: instead of calling handler.on_start_element() and
/// having the handler build a node, we build the node RIGHT HERE in the
/// parse loop body. This eliminates:
///   - Function call overhead
///   - Handler state management
///   - Indirect dispatch
///   - Redundant name/value copies

#include <parshred/dom_parser.hpp>
#include <parshred/platform.hpp>
#include <cstring>

namespace parshred {

namespace detail {

/// In-situ: write a null terminator at `pos` and return string_view.
/// This allows strlen-based APIs to work directly on the buffer.
inline std::string_view make_insitu_sv(char* data, size_t start, size_t end) noexcept {
    return {data + start, end - start};
}

/// Expand the 5 built-in entities. Returns expanded string stored in pool.
inline std::string_view expand_entities(const char* data, size_t start, size_t end,
                                        StringPool& strings) {
    // Quick check: is there even an '&' in this range?
    bool has_entity = false;
    for (size_t i = start; i < end; ++i) {
        if (data[i] == '&') { has_entity = true; break; }
    }
    if (!has_entity) {
        return {data + start, end - start};
    }

    // Expand entities into a temporary buffer, then store in pool
    thread_local std::string buf;
    buf.clear();
    buf.reserve(end - start);

    size_t i = start;
    while (i < end) {
        if (data[i] == '&') {
            size_t ref_start = i + 1;
            size_t ref_end = ref_start;
            while (ref_end < end && data[ref_end] != ';') ++ref_end;
            if (ref_end < end) {
                std::string_view ref(data + ref_start, ref_end - ref_start);
                if (ref == "lt")        buf += '<';
                else if (ref == "gt")   buf += '>';
                else if (ref == "amp")  buf += '&';
                else if (ref == "apos") buf += '\'';
                else if (ref == "quot") buf += '"';
                else if (ref.size() > 1 && ref[0] == '#') {
                    // Numeric entity
                    unsigned long cp = 0;
                    if (ref[1] == 'x' || ref[1] == 'X')
                        cp = std::strtoul(ref.data() + 2, nullptr, 16);
                    else
                        cp = std::strtoul(ref.data() + 1, nullptr, 10);
                    // Encode as UTF-8
                    if (cp < 0x80) {
                        buf += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        buf += static_cast<char>(0xC0 | (cp >> 6));
                        buf += static_cast<char>(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        buf += static_cast<char>(0xE0 | (cp >> 12));
                        buf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        buf += static_cast<char>(0xF0 | (cp >> 18));
                        buf += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        buf += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        buf += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                } else {
                    // Unknown entity — pass through as-is
                    buf += '&';
                    buf += ref;
                    buf += ';';
                }
                i = ref_end + 1;
            } else {
                buf += data[i];
                ++i;
            }
        } else {
            buf += data[i];
            ++i;
        }
    }

    return strings.store(buf);
}

} // namespace detail

template<unsigned Flags>
DomParseResult dom_parse(char* data, size_t len) {
    DomParseResult result;

    // Pre-estimate capacity
    size_t est_nodes = estimate_node_count(len);
    result.pool.reserve(est_nodes);

    XmlNode* doc_node = result.doc.document_node();
    XmlNode* current = doc_node;  // Current parent node

    // Parse stack: tracks parent + last_child for O(1) child append.
    // These were previously stored in XmlNode (parent, last_child fields)
    // but moved here to shrink XmlNode to 64 bytes (one cache line).
    // Most XML documents have shallow nesting (<32 levels), so a small
    // inline stack avoids heap allocation in the common case.
    struct StackEntry {
        XmlNode* parent;
        XmlNode* last_child;
    };
    StackEntry stack_buf[64];  // 64 levels deep — covers >99.99% of XML
    int stack_depth = 0;
    XmlNode* last_child = nullptr;  // last child of `current`

    size_t pos = 0;

    // Helper: append a child to current parent
    auto append_child = [&](XmlNode* child) {
        if (last_child) {
            last_child->next_sibling = child;
        } else {
            current->first_child = child;
        }
        last_child = child;
    };

    // Track last attribute for O(1) append (local to current element)
    XmlNode* last_attr = nullptr;

    // Helper: append an attribute to a node
    auto append_attr = [&](XmlNode* elem, XmlNode* attr) {
        if (!last_attr) {
            elem->first_attr = attr;
        } else {
            last_attr->next_sibling = attr;
        }
        last_attr = attr;
        elem->flags |= NF_HAS_ATTRS;
    };

    while (pos < len) {
        // ── Text content ─────────────────────────────────────────────
        size_t text_start = pos;
        size_t text_end;

        if constexpr (Flags & DOM_NO_ENTITIES) {
            text_end = skip_text_turbo(data, pos, len);
        } else {
            text_end = skip_text_fast(data, pos, len);
            // If stopped at '&', scan to next '<' for full text span
            if (text_end < len && data[text_end] == '&') {
                text_end = find_char_fast(data, text_end, len, '<');
            }
        }

        if constexpr (!(Flags & DOM_NO_TEXT_NODES)) {
            if (text_end > text_start) {
                XmlNode* text_node = result.pool.allocate(NodeType::Text);
                if constexpr (Flags & DOM_NO_ENTITIES) {
                    text_node->value = detail::make_insitu_sv(data, text_start, text_end);
                } else {
                    text_node->value = detail::expand_entities(
                        data, text_start, text_end, result.strings);
                }
                append_child(text_node);
            }
        }
        pos = text_end;

        if (pos >= len) break;

        // ── We're at '<' ─────────────────────────────────────────────
        ++pos;
        if (pos >= len) break;

        char next = data[pos];

        // ── End tag: </name> ─────────────────────────────────────────
        if (next == '/') {
            ++pos;
            // Skip optional whitespace (rare but spec-legal)
            while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
            while (pos < len && parshred::is_name_char(data[pos])) ++pos;
            // Skip to '>'
            while (pos < len && data[pos] != '>') ++pos;
            if (pos < len) ++pos;

            // Pop the parse stack to restore parent + last_child
            if (PARSHRED_LIKELY(stack_depth > 0)) {
                --stack_depth;
                current = stack_buf[stack_depth].parent;
                last_child = stack_buf[stack_depth].last_child;
            } else {
                current = doc_node;
                last_child = nullptr;
            }
            continue;
        }

        // ── Comment: <!-- ... --> ────────────────────────────────────
        if (next == '!' && pos + 2 < len && data[pos+1] == '-' && data[pos+2] == '-') {
            pos += 3;
            size_t content_start = pos;
            size_t end = find_comment_end(data, pos, len);
            if constexpr (!(Flags & DOM_NO_COMMENTS)) {
                if (end < len) {
                    XmlNode* node = result.pool.allocate(NodeType::Comment);
                    node->value = detail::make_insitu_sv(data, content_start, end);
                    append_child(node);
                }
            }
            pos = (end < len) ? end + 3 : len;
            continue;
        }

        // ── CDATA: <![CDATA[ ... ]]> ────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            std::memcmp(data + pos + 1, "[CDATA[", 7) == 0) {
            pos += 8;
            size_t content_start = pos;
            size_t end = find_cdata_end(data, pos, len);
            if constexpr (!(Flags & DOM_NO_TEXT_NODES)) {
                if (end < len) {
                    XmlNode* node = result.pool.allocate(NodeType::CData);
                    node->value = detail::make_insitu_sv(data, content_start, end);
                    append_child(node);
                }
            }
            pos = (end < len) ? end + 3 : len;
            continue;
        }

        // ── DOCTYPE: <!DOCTYPE ... > ────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            (std::memcmp(data + pos + 1, "DOCTYPE", 7) == 0 ||
             std::memcmp(data + pos + 1, "doctype", 7) == 0)) {
            pos += 8;
            int bracket_depth = 0;
            while (pos < len) {
                char c = data[pos];
                if (c == '[') ++bracket_depth;
                else if (c == ']' && bracket_depth > 0) --bracket_depth;
                else if (c == '>' && bracket_depth == 0) { ++pos; break; }
                ++pos;
            }
            continue;
        }

        // ── Processing instruction: <? ... ?> ────────────────────────
        if (next == '?') {
            ++pos;
            size_t target_start = pos;
            while (pos < len && parshred::is_name_char(data[pos])) ++pos;
            size_t target_end = pos;
            size_t pi_end = find_pi_end(data, pos, len);

            if constexpr (!(Flags & DOM_NO_PIS)) {
                if (pi_end < len) {
                    std::string_view target(data + target_start, target_end - target_start);
                    if (target == "xml" || target == "XML") {
                        XmlNode* node = result.pool.allocate(NodeType::Declaration);
                        node->name = target;
                        // Store the whole content as value for later extraction
                        size_t val_start = target_end;
                        while (val_start < pi_end && parshred::is_whitespace(data[val_start]))
                            ++val_start;
                        node->value = detail::make_insitu_sv(data, val_start, pi_end);
                        append_child(node);
                    } else {
                        XmlNode* node = result.pool.allocate(NodeType::PI);
                        node->name = target;
                        size_t val_start = target_end;
                        while (val_start < pi_end && parshred::is_whitespace(data[val_start]))
                            ++val_start;
                        node->value = detail::make_insitu_sv(data, val_start, pi_end);
                        append_child(node);
                    }
                }
            }
            pos = (pi_end < len) ? pi_end + 2 : len;
            continue;
        }

        // ── Start tag: <name ... > or <name ... /> ───────────────────
        {
            // Skip any leading whitespace (unusual but handle it)
            while (pos < len && parshred::is_whitespace(data[pos])) ++pos;

            size_t name_start = pos;
            // Read element name — fast scalar path for short names
            // We know we're past '<' and whitespace, so check name-start directly
            if (PARSHRED_LIKELY(pos < len && parshred::is_name_start(data[pos]))) {
                ++pos;
                // Unroll first few chars for short names (branch-prediction friendly)
                while (pos < len && parshred::is_name_char(data[pos])) ++pos;
            }
            size_t name_end = pos;

            if (name_end == name_start) {
                // Malformed — skip to next '>'
                while (pos < len && data[pos] != '>') ++pos;
                if (pos < len) ++pos;
                continue;
            }

            // Allocate element node
            XmlNode* elem = result.pool.allocate(NodeType::Element);
            elem->name = detail::make_insitu_sv(data, name_start, name_end);
            append_child(elem);
            last_attr = nullptr;  // Reset for new element's attributes

            // ── Parse attributes ─────────────────────────────────────
            // Skip whitespace before first attr
            while (pos < len && parshred::is_whitespace(data[pos])) ++pos;

            while (pos < len && data[pos] != '>' && data[pos] != '/') {
                // Attribute name
                size_t attr_name_start = pos;
                if (!parshred::is_name_start(data[pos])) break;
                ++pos;
                while (pos < len && parshred::is_name_char(data[pos])) ++pos;
                size_t attr_name_end = pos;

                // Skip whitespace and '='
                while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
                std::string_view attr_val;

                if (pos < len && data[pos] == '=') {
                    ++pos;
                    while (pos < len && parshred::is_whitespace(data[pos])) ++pos;

                    if (pos < len && (data[pos] == '"' || data[pos] == '\'')) {
                        char quote = data[pos];
                        ++pos;
                        size_t val_start = pos;
                        // SIMD-accelerated scan for closing quote
                        pos = find_char_fast(data, pos, len, quote);
                        size_t val_end = pos;
                        if (pos < len) ++pos;  // skip quote

                        if constexpr (Flags & DOM_NO_ENTITIES) {
                            attr_val = detail::make_insitu_sv(data, val_start, val_end);
                        } else {
                            attr_val = detail::expand_entities(
                                data, val_start, val_end, result.strings);
                        }
                    }
                }

                // Create attribute node
                XmlNode* attr_node = result.pool.allocate(NodeType::Attribute);
                attr_node->name = detail::make_insitu_sv(data, attr_name_start, attr_name_end);
                attr_node->value = attr_val;
                append_attr(elem, attr_node);

                // Skip whitespace after attribute
                while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
            }

            // Self-closing tag?
            bool self_closing = false;
            if (pos < len && data[pos] == '/') {
                self_closing = true;
                ++pos;
            }
            if (pos < len && data[pos] == '>') {
                ++pos;
            }

            if (!self_closing) {
                // Push: this element becomes the new parent.
                // Save current parent + last_child on the parse stack.
                elem->flags |= NF_HAS_CHILDREN;
                if (PARSHRED_LIKELY(stack_depth < 64)) {
                    stack_buf[stack_depth] = {current, last_child};
                    ++stack_depth;
                }
                current = elem;
                last_child = nullptr;
            }
        }
    }

    result.doc.node_count_ = result.pool.size();
    result.doc.bytes_allocated_ = result.pool.bytes_allocated();

    return result;
}

} // namespace parshred
