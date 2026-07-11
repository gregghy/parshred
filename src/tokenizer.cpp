/// @file tokenizer.cpp
/// @brief XML tokenizer — converts structural index into a token stream.

#include <parshred/tokenizer.hpp>
#include <algorithm>
#include <cstring>

namespace parshred {

// ── Character classification ──────────────────────────────────────────

bool Tokenizer::is_name_start_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == ':' || static_cast<unsigned char>(c) >= 0x80;
}

bool Tokenizer::is_name_char(char c) noexcept {
    return is_name_start_char(c) || (c >= '0' && c <= '9') ||
           c == '-' || c == '.';
}

bool Tokenizer::is_whitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

size_t Tokenizer::skip_whitespace(size_t pos) const noexcept {
    while (pos < input_.size() && is_whitespace(input_[pos])) ++pos;
    return pos;
}

size_t Tokenizer::read_name(size_t pos) const noexcept {
    if (pos >= input_.size() || !is_name_start_char(input_[pos])) return pos;
    while (pos < input_.size() && is_name_char(input_[pos])) ++pos;
    return pos;
}

// ── Public API ────────────────────────────────────────────────────────

void Tokenizer::tokenize(std::span<const char> input) {
    auto idx = simd_scan(input);
    tokenize(input, idx);
}

void Tokenizer::tokenize(std::span<const char> input, const StructuralIndex& index) {
    tokens_.clear();
    input_ = input;

    if (input.empty()) return;

    // Reserve estimate
    tokens_.reserve(index.positions.size());

    size_t n = index.positions.size();
    size_t text_start = 0; // track text between tags

    for (size_t si = 0; si < n; ++si) {
        uint32_t pos = index.positions[si];
        uint8_t ch = index.chars[si];

        if (ch == '<') {
            // Emit any text before this '<'
            if (pos > text_start) {
                process_text(text_start, pos);
            }

            // Find the matching '>'
            size_t close_si = si + 1;
            // For comments, CDATA, PI, DOCTYPE — we need to find the correct closing sequence
            size_t next_pos = pos + 1;

            if (next_pos < input_.size()) {
                if (input_[next_pos] == '!') {
                    // Could be comment, CDATA, or DOCTYPE
                    if (next_pos + 1 < input_.size() && input_[next_pos + 1] == '-' &&
                        next_pos + 2 < input_.size() && input_[next_pos + 2] == '-') {
                        // Comment: <!-- ... -->
                        process_comment(pos);
                        // Find end of comment (-->)
                        const char* pattern = "-->";
                        const char* found = std::search(input_.data() + pos + 4,
                                                        input_.data() + input_.size(),
                                                        pattern, pattern + 3);
                        if (found != input_.data() + input_.size()) {
                            text_start = static_cast<size_t>(found - input_.data()) + 3;
                        } else {
                            text_start = input_.size();
                        }
                        // Skip structural indices that fall within the comment
                        while (si + 1 < n && index.positions[si + 1] < text_start) ++si;
                        continue;
                    }
                    if (next_pos + 7 < input_.size() &&
                        std::memcmp(input_.data() + next_pos + 1, "[CDATA[", 7) == 0) {
                        // CDATA: <![CDATA[ ... ]]>
                        process_cdata(pos);
                        const char* pattern = "]]>";
                        const char* found = std::search(input_.data() + pos + 9,
                                                        input_.data() + input_.size(),
                                                        pattern, pattern + 3);
                        if (found != input_.data() + input_.size()) {
                            text_start = static_cast<size_t>(found - input_.data()) + 3;
                        } else {
                            text_start = input_.size();
                        }
                        while (si + 1 < n && index.positions[si + 1] < text_start) ++si;
                        continue;
                    }
                    if (next_pos + 7 < input_.size() &&
                        (std::memcmp(input_.data() + next_pos + 1, "DOCTYPE", 7) == 0 ||
                         std::memcmp(input_.data() + next_pos + 1, "doctype", 7) == 0)) {
                        // Scan forward through the raw input to handle internal subset [...]
                        // We need to find the closing '>' that is NOT inside brackets
                        size_t scan = pos + 9; // skip past "<!DOCTYPE"
                        int bracket_depth = 0;
                        while (scan < input_.size()) {
                            char sc = input_[scan];
                            if (sc == '[') {
                                ++bracket_depth;
                            } else if (sc == ']') {
                                if (bracket_depth > 0) --bracket_depth;
                            } else if (sc == '>' && bracket_depth == 0) {
                                break;
                            }
                            ++scan;
                        }
                        size_t doctype_end = (scan < input_.size()) ? scan + 1 : input_.size();
                        // Emit with full DOCTYPE text
                        tokens_.push_back({TokenType::DocType,
                            std::string_view(input_.data() + pos, doctype_end - pos),
                            pos});
                        text_start = doctype_end;
                        // Skip structural indices that fall within the DOCTYPE
                        while (si + 1 < n && index.positions[si + 1] < text_start) ++si;
                        continue;
                    }
                }
                if (input_[next_pos] == '?') {
                    // Processing instruction or XML declaration
                    process_pi(pos);
                    // Find ?>
                    const char* pattern = "?>";
                    const char* found = std::search(input_.data() + pos + 2,
                                                    input_.data() + input_.size(),
                                                    pattern, pattern + 2);
                    if (found != input_.data() + input_.size()) {
                        text_start = static_cast<size_t>(found - input_.data()) + 2;
                    } else {
                        text_start = input_.size();
                    }
                    while (si + 1 < n && index.positions[si + 1] < text_start) ++si;
                    continue;
                }
            }

            // Normal tag: find the closing '>'
            while (close_si < n && index.chars[close_si] != '>') ++close_si;

            if (close_si < n) {
                size_t tag_end = index.positions[close_si];
                process_tag(pos, tag_end);
                text_start = tag_end + 1;
                si = close_si;
            } else {
                throw ParseError("Unclosed tag", pos);
            }
        }
        // Other structural chars (>, /, ", ', =, &) are handled within
        // process_tag or as part of the quote-masked scanning.
    }

    // Emit any trailing text
    if (text_start < input_.size()) {
        process_text(text_start, input_.size());
    }
}

// ── Internal helpers ──────────────────────────────────────────────────

void Tokenizer::process_tag(size_t start, size_t end) {
    // start points to '<', end points to '>'
    size_t pos = start + 1;

    bool is_end_tag = false;
    bool is_self_closing = false;

    if (pos < end && input_[pos] == '/') {
        is_end_tag = true;
        ++pos;
    }

    // Read tag name
    pos = skip_whitespace(pos);
    size_t name_start = pos;
    pos = read_name(pos);
    size_t name_end = pos;

    if (name_start == name_end) {
        throw ParseError("Expected tag name", start);
    }

    std::string_view name(input_.data() + name_start, name_end - name_start);

    // Check for self-closing: last char before '>' is '/'
    if (end > start + 1 && input_[end - 1] == '/') {
        is_self_closing = true;
    }

    if (is_end_tag) {
        tokens_.push_back({TokenType::EndTag, name, start});
        return;
    }

    tokens_.push_back({
        is_self_closing ? TokenType::SelfClosingTag : TokenType::StartTag,
        name, start
    });

    // Parse attributes
    pos = skip_whitespace(pos);
    while (pos < end) {
        pos = skip_whitespace(pos);
        if (pos >= end) break;

        // Check for self-closing slash at end
        if (input_[pos] == '/' && pos + 1 >= end) break;
        if (input_[pos] == '>') break;

        // Read attribute name
        size_t attr_name_start = pos;
        pos = read_name(pos);
        if (pos == attr_name_start) break; // no more attributes

        std::string_view attr_name(input_.data() + attr_name_start, pos - attr_name_start);
        tokens_.push_back({TokenType::AttributeName, attr_name, attr_name_start});

        // Skip whitespace and '='
        pos = skip_whitespace(pos);
        if (pos < end && input_[pos] == '=') {
            ++pos;
            pos = skip_whitespace(pos);

            // Read attribute value (quoted)
            if (pos < end && (input_[pos] == '"' || input_[pos] == '\'')) {
                char quote = input_[pos];
                ++pos;
                size_t val_start = pos;
                while (pos < input_.size() && input_[pos] != quote) ++pos;
                std::string_view attr_val(input_.data() + val_start, pos - val_start);
                tokens_.push_back({TokenType::AttributeValue, attr_val, val_start});
                if (pos < input_.size()) ++pos; // skip closing quote
            }
        }
    }
}

void Tokenizer::process_text(size_t start, size_t end) {
    // Skip pure-whitespace text nodes? No — emit everything, let the parser decide.
    std::string_view text(input_.data() + start, end - start);

    // Check for entity references within the text
    // For now, emit the whole text as one token. Entity expansion is the parser's job.
    // But we do detect standalone &entity; references and emit them.
    size_t pos = start;
    size_t text_begin = start;

    while (pos < end) {
        if (input_[pos] == '&') {
            // Emit text before the entity
            if (pos > text_begin) {
                tokens_.push_back({TokenType::Text,
                    std::string_view(input_.data() + text_begin, pos - text_begin),
                    text_begin});
            }
            // Find the ';'
            size_t semi = pos + 1;
            while (semi < end && input_[semi] != ';') ++semi;
            if (semi < end) {
                tokens_.push_back({TokenType::EntityRef,
                    std::string_view(input_.data() + pos, semi - pos + 1),
                    pos});
                pos = semi + 1;
                text_begin = pos;
            } else {
                // No closing ';' — emit as text
                ++pos;
            }
        } else {
            ++pos;
        }
    }

    // Emit remaining text
    if (text_begin < end) {
        tokens_.push_back({TokenType::Text,
            std::string_view(input_.data() + text_begin, end - text_begin),
            text_begin});
    }
}

void Tokenizer::process_comment(size_t start) {
    // start points to '<' in '<!--'
    // Find '-->'
    const char* pattern = "-->";
    const char* found = std::search(input_.data() + start + 4,
                                    input_.data() + input_.size(),
                                    pattern, pattern + 3);
    size_t content_start = start + 4;
    size_t content_end;
    if (found != input_.data() + input_.size()) {
        content_end = static_cast<size_t>(found - input_.data());
    } else {
        content_end = input_.size();
    }
    tokens_.push_back({TokenType::Comment,
        std::string_view(input_.data() + content_start, content_end - content_start),
        start});
}

void Tokenizer::process_cdata(size_t start) {
    // start points to '<' in '<![CDATA['
    const char* pattern = "]]>";
    const char* found = std::search(input_.data() + start + 9,
                                    input_.data() + input_.size(),
                                    pattern, pattern + 3);
    size_t content_start = start + 9;
    size_t content_end;
    if (found != input_.data() + input_.size()) {
        content_end = static_cast<size_t>(found - input_.data());
    } else {
        content_end = input_.size();
    }
    tokens_.push_back({TokenType::CData,
        std::string_view(input_.data() + content_start, content_end - content_start),
        start});
}

void Tokenizer::process_pi(size_t start) {
    // start points to '<' in '<?'
    size_t pos = start + 2;
    pos = skip_whitespace(pos);

    // Read target name
    size_t target_start = pos;
    pos = read_name(pos);

    std::string_view target(input_.data() + target_start, pos - target_start);

    // Check for <?xml ...?> — XML declaration
    if (target == "xml" || target == "XML") {
        // Find '?>'
        const char* pattern = "?>";
        const char* found = std::search(input_.data() + pos,
                                        input_.data() + input_.size(),
                                        pattern, pattern + 2);
        size_t data_end;
        if (found != input_.data() + input_.size()) {
            data_end = static_cast<size_t>(found - input_.data());
        } else {
            data_end = input_.size();
        }
        tokens_.push_back({TokenType::XmlDeclaration,
            std::string_view(input_.data() + start, data_end + 2 - start),
            start});
    } else {
        // Regular PI
        const char* pattern = "?>";
        const char* found = std::search(input_.data() + pos,
                                        input_.data() + input_.size(),
                                        pattern, pattern + 2);
        size_t data_end;
        if (found != input_.data() + input_.size()) {
            data_end = static_cast<size_t>(found - input_.data());
        } else {
            data_end = input_.size();
        }

        std::string_view pi_content(input_.data() + start, data_end + 2 - start);
        tokens_.push_back({TokenType::ProcessingInstruction, pi_content, start});
    }
}

} // namespace parshred
