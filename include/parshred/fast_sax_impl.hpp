#pragma once
/// @file fast_sax_impl.hpp
/// @brief Inline implementation of FastSaxParser::parse template.
///
/// Separated from fast_sax.hpp so that the template body is visible
/// at point of use for any handler type, while keeping the header
/// declarations clean.

#include <parshred/fast_sax.hpp>
#include <parshred/simd_utils.hpp>
#include <parshred/lookup_tables.hpp>

#include <cstring>

namespace parshred {

template<ParseMode Mode>
template<typename Handler>
void FastSaxParser<Mode>::parse(const char* data, size_t len, Handler& handler) {
    // Reset state
    depth_ = 0;
    entity_count_ = 0;
    if constexpr (Mode == ParseMode::Normal) {
        stats_ = {};
        stats_.bytes_parsed = len;
    }
    parse_chunk(data, len, handler);
}

template<ParseMode Mode>
template<typename Handler>
void FastSaxParser<Mode>::parse_chunk(const char* data, size_t len, Handler& handler) {
    size_t pos = 0;

    while (pos < len) {
        // ── Skip text content ────────────────────────────────────────
        size_t text_start = pos;
        size_t text_end;

        if constexpr (Mode == ParseMode::Turbo) {
            text_end = skip_text_turbo(data, pos, len);
        } else {
            text_end = skip_text_fast(data, pos, len);
        }

        // In Normal mode, if we stopped at '&', grab everything up to
        // the next '<' and run entity expansion on the whole span.
        if constexpr (Mode == ParseMode::Normal) {
            if (text_end < len && data[text_end] == '&') {
                size_t full_end = find_char_fast(data, text_end, len, '<');
                if (full_end > text_start) {
                    ++stats_.text_nodes;
                    expand_and_emit_text(
                        std::string_view(data + text_start, full_end - text_start),
                        handler);
                }
                pos = full_end;
                continue;
            }
        }

        if (text_end > text_start) {
            if constexpr (Mode == ParseMode::Normal) {
                ++stats_.text_nodes;
            }
            handler.on_text(std::string_view(data + text_start, text_end - text_start));
            pos = text_end;
        }

        if (pos >= len) break;

        // ── We're at '<' ─────────────────────────────────────────────
        pos++;  // skip '<'
        if (pos >= len) {
            throw ParseError("Unexpected end of input after '<'", pos - 1);
        }

        char next = data[pos];

        // ── End tag: </name> ─────────────────────────────────────────
        if (next == '/') {
            ++pos;
            pos = skip_whitespace_fast(data, pos, len);
            size_t name_start = pos;
            pos = read_name_fast(data, pos, len);

            if (pos == name_start) {
                throw ParseError("Expected tag name after '</'", pos);
            }

            std::string_view name(data + name_start, pos - name_start);

            // Skip to '>'
            pos = skip_whitespace_fast(data, pos, len);
            if (pos < len && data[pos] == '>') {
                ++pos;
            } else {
                throw ParseError("Expected '>' in end tag", pos);
            }

            if constexpr (Mode == ParseMode::Normal) {
                if (depth_ == 0) throw ParseError("Unexpected end tag", name_start);
                --depth_;
            }

            handler.on_end_element(name);
            continue;
        }

        // ── Comment: <!-- ... --> ────────────────────────────────────
        if (next == '!' && pos + 2 < len && data[pos + 1] == '-' && data[pos + 2] == '-') {
            pos += 3;  // skip "!--"
            size_t content_start = pos;
            size_t end = find_comment_end(data, pos, len);
            if (end >= len) {
                pos = len;
            } else {
                if constexpr (Mode == ParseMode::Normal) {
                    ++stats_.comments;
                }
                handler.on_comment(std::string_view(data + content_start, end - content_start));
                pos = end + 3;  // skip "-->"
            }
            continue;
        }

        // ── CDATA: <![CDATA[ ... ]]> ────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            std::memcmp(data + pos + 1, "[CDATA[", 7) == 0) {
            pos += 8;  // skip "![CDATA["
            size_t content_start = pos;
            size_t end = find_cdata_end(data, pos, len);
            if (end >= len) {
                pos = len;
            } else {
                if constexpr (Mode == ParseMode::Normal) {
                    ++stats_.cdata_nodes;
                }
                handler.on_cdata(std::string_view(data + content_start, end - content_start));
                pos = end + 3;  // skip "]]>"
            }
            continue;
        }

        // ── DOCTYPE: <!DOCTYPE ... > ────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            (std::memcmp(data + pos + 1, "DOCTYPE", 7) == 0 ||
             std::memcmp(data + pos + 1, "doctype", 7) == 0)) {
            pos += 8;  // skip "!DOCTYPE"
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
            pos = skip_whitespace_fast(data, pos, len);
            size_t target_start = pos;
            pos = read_name_fast(data, pos, len);
            std::string_view target(data + target_start, pos - target_start);

            size_t pi_end = find_pi_end(data, pos, len);
            if (pi_end >= len) {
                pos = len;
                continue;
            }

            if (target == "xml" || target == "XML") {
                // XML declaration — parse version/encoding/standalone
                std::string_view content(data + target_start,
                                         pi_end - target_start);
                auto extract = [&](const char* attr_name) -> std::string_view {
                    auto p = content.find(attr_name);
                    if (p == std::string_view::npos) return {};
                    p = content.find('=', p);
                    if (p == std::string_view::npos) return {};
                    ++p;
                    while (p < content.size() &&
                           (content[p] == ' ' || content[p] == '"' || content[p] == '\''))
                        ++p;
                    size_t s = p;
                    while (p < content.size() && content[p] != '"' && content[p] != '\'')
                        ++p;
                    return content.substr(s, p - s);
                };
                handler.on_xml_declaration(extract("version"),
                                           extract("encoding"),
                                           extract("standalone"));
            } else {
                // Regular PI
                size_t data_start = pos;
                while (data_start < pi_end && parshred::is_whitespace(data[data_start]))
                    ++data_start;
                std::string_view pi_data(data + data_start, pi_end - data_start);
                handler.on_processing_instruction(target, pi_data);
            }
            pos = pi_end + 2;  // skip "?>"
            continue;
        }

        // ── Start tag or self-closing tag: <name ... > or <name ... /> ───
        {
            pos = skip_whitespace_fast(data, pos, len);
            size_t name_start = pos;
            pos = read_name_fast(data, pos, len);

            if (pos == name_start) {
                throw ParseError("Expected tag name", pos);
            }

            std::string_view tag_name(data + name_start, pos - name_start);

            // ── Parse attributes ─────────────────────────────────────
            size_t nattrs = 0;

            pos = skip_whitespace_fast(data, pos, len);

            while (pos < len && data[pos] != '>' && data[pos] != '/') {
                // Attribute name
                size_t attr_name_start = pos;
                pos = read_name_fast(data, pos, len);
                if (pos == attr_name_start) break;  // no more attributes

                std::string_view attr_name(data + attr_name_start, pos - attr_name_start);

                // Skip whitespace and '='
                pos = skip_whitespace_fast(data, pos, len);
                std::string_view attr_val;

                if (pos < len && data[pos] == '=') {
                    ++pos;
                    pos = skip_whitespace_fast(data, pos, len);

                    // Read quoted value
                    if (pos < len && (data[pos] == '"' || data[pos] == '\'')) {
                        char quote = data[pos];
                        ++pos;
                        size_t val_start = pos;
                        // SIMD-accelerated quote search
                        pos = skip_attr_value(data, pos, len, quote);
                        attr_val = std::string_view(data + val_start, pos - val_start);
                        if (pos < len) ++pos;  // skip closing quote
                    }
                }

                if constexpr (Mode == ParseMode::Normal) {
                    if (nattrs >= max_attribute_count_) {
                        throw SecurityError("Maximum attribute count exceeded", attr_name_start);
                    }
                }

                // Store attribute
                if (nattrs < 64) {
                    attrs_buf_[nattrs] = {attr_name, attr_val};
                } else {
                    if (nattrs == 64) {
                        // Spill to overflow vector
                        attrs_overflow_.assign(attrs_buf_, attrs_buf_ + 64);
                    }
                    attrs_overflow_.push_back({attr_name, attr_val});
                }
                ++nattrs;

                pos = skip_whitespace_fast(data, pos, len);
            }

            // Determine if self-closing
            bool self_closing = false;
            if (pos < len && data[pos] == '/') {
                self_closing = true;
                ++pos;
            }
            if (pos < len && data[pos] == '>') {
                ++pos;
            } else {
                throw ParseError("Expected '>' in start tag", pos);
            }

            if constexpr (Mode == ParseMode::Normal) {
                if (!self_closing) {
                    if (++depth_ > max_depth_)
                        throw SecurityError("Maximum nesting depth exceeded", name_start);
                }
                ++stats_.elements;
                stats_.attributes += nattrs;
            }

            const Attribute* attr_ptr = (nattrs <= 64) ? attrs_buf_ : attrs_overflow_.data();
            handler.on_start_element(tag_name, attr_ptr, nattrs);

            if (self_closing) {
                handler.on_end_element(tag_name);
            }
        }
    }
}

} // namespace parshred
