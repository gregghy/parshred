#pragma once
/// @file pull_parser.hpp
/// @brief StAX / xmlReader-style pull parser for parshred.
///
/// XmlReader is a lazy, forward-only pull parser.  Each call to next()
/// advances one event and returns true; the caller inspects event_type()
/// and the accessor methods to consume it.
///
/// The implementation is a direct single-pass state machine that shares
/// the same SIMD scanning primitives used by FastSaxParser, so hot-path
/// scanning (whitespace, names, text content, multi-char terminators) is
/// already vectorised on AVX2 / SSE4.2 machines.
///
/// Design notes
/// ─────────────
/// • Header-only – no separate .cpp required.
/// • Zero-copy – all string_view accessors point directly into the
///   original input buffer; nothing is heap-allocated for the common
///   case (≤8 attributes).
/// • SmallVector-style attribute storage: 8 slots on the stack; spill
///   to std::vector only when an element has more than 8 attributes.
/// • Self-closing tags (<br/>) fire StartElement then, on the *next*
///   next() call, EndElement without re-scanning any input.
/// • DOCTYPE declarations are silently skipped.
/// • Thread safety: each XmlReader is an independent object; safe to use
///   from one thread at a time.
///
/// Typical usage:
/// @code
///   parshred::XmlReader reader(xml_data, xml_len);
///   while (reader.next()) {
///       if (reader.is_start_element("item")) {
///           auto id   = reader.attribute("id");
///           auto text = reader.read_element_text();
///       }
///   }
/// @endcode

#include <parshred/common.hpp>
#include <parshred/lookup_tables.hpp>
#include <parshred/simd_utils.hpp>

#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace parshred {

// ─────────────────────────────────────────────────────────────────────────────
// XmlEvent  — the discriminant returned by event_type()
// ─────────────────────────────────────────────────────────────────────────────

enum class XmlEvent : uint8_t {
    StartDocument,          ///< Synthetic first event (before any content)
    EndDocument,            ///< Input exhausted; next() returned false just before
    StartElement,           ///< <tag …>  or  <tag …/>  (start half)
    EndElement,             ///< </tag>   or  <tag …/>  (end half, synthetic)
    Text,                   ///< Character data between tags
    CData,                  ///< <![CDATA[ … ]]>
    Comment,                ///< <!-- … -->
    ProcessingInstruction,  ///< <?target data?>  (non-xml)
    XmlDeclaration,         ///< <?xml … ?>
};

// ─────────────────────────────────────────────────────────────────────────────
// XmlReader
// ─────────────────────────────────────────────────────────────────────────────

class XmlReader {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    XmlReader(const char* data, size_t len) noexcept
        : data_(data), len_(len) {}

    explicit XmlReader(std::string_view input) noexcept
        : data_(input.data()), len_(input.size()) {}

    // ── Core iteration ───────────────────────────────────────────────────────

    /// Advance to the next event.
    /// @return true  – a new event is available via event_type() and friends.
    /// @return false – the document is fully consumed (EndDocument was set).
    bool next();

    // ── Current-event accessors ──────────────────────────────────────────────

    /// The type of the current event.
    [[nodiscard]] XmlEvent event_type() const noexcept { return event_; }

    /// Element name for StartElement / EndElement events.
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    /// Text content for Text / CData / Comment events.
    [[nodiscard]] std::string_view text() const noexcept { return text_; }

    /// PI data (ProcessingInstruction), or text content alias (Text/CData/Comment).
    [[nodiscard]] std::string_view value() const noexcept {
        return (event_ == XmlEvent::ProcessingInstruction) ? pi_data_ : text_;
    }

    // ── Attribute accessors (valid on StartElement) ──────────────────────────

    /// Number of attributes on the current StartElement.
    [[nodiscard]] size_t attribute_count() const noexcept { return nattrs_; }

    /// Name of the i-th attribute (0-based).
    [[nodiscard]] std::string_view attribute_name(size_t i) const noexcept {
        return attr_at(i).name;
    }

    /// Value of the i-th attribute (0-based).
    [[nodiscard]] std::string_view attribute_value(size_t i) const noexcept {
        return attr_at(i).value;
    }

    /// Find an attribute by name; returns empty string_view if not found.
    [[nodiscard]] std::string_view attribute(std::string_view attr_name) const noexcept {
        for (size_t i = 0; i < nattrs_; ++i) {
            if (attr_at(i).name == attr_name)
                return attr_at(i).value;
        }
        return {};
    }

    // ── Position ─────────────────────────────────────────────────────────────

    /// Current element nesting depth (0 = document level).
    [[nodiscard]] size_t depth() const noexcept { return depth_; }

    /// 1-based line number of the *start* of the current event.
    [[nodiscard]] size_t line() const noexcept { return event_line_; }

    /// 1-based column of the *start* of the current event.
    [[nodiscard]] size_t column() const noexcept { return event_col_; }

    // ── Convenience predicates ───────────────────────────────────────────────

    [[nodiscard]] bool is_start_element() const noexcept {
        return event_ == XmlEvent::StartElement;
    }
    [[nodiscard]] bool is_start_element(std::string_view tag) const noexcept {
        return event_ == XmlEvent::StartElement && name_ == tag;
    }

    [[nodiscard]] bool is_end_element() const noexcept {
        return event_ == XmlEvent::EndElement;
    }
    [[nodiscard]] bool is_end_element(std::string_view tag) const noexcept {
        return event_ == XmlEvent::EndElement && name_ == tag;
    }

    // ── Composite helpers ────────────────────────────────────────────────────

    /// Collect and return all text content until the matching end tag for the
    /// *current* StartElement.  Leaves the reader positioned just *after* the
    /// EndElement (i.e., the next next() call returns the event after </tag>).
    ///
    /// Concatenates Text and CData contributions; ignores Comments, PIs, and
    /// nested elements.
    ///
    /// @pre event_type() == XmlEvent::StartElement
    std::string read_element_text();

    /// Skip the current element and all its descendants.
    /// Advances past the matching EndElement.
    ///
    /// @pre event_type() == XmlEvent::StartElement
    void skip_element();

private:
    // ── SmallVector for attributes ───────────────────────────────────────────

    static constexpr size_t kInlineAttrs = 8;

    std::array<Attribute, kInlineAttrs> attrs_inline_{};
    std::vector<Attribute>              attrs_spill_;   // used only when > 8 attrs
    size_t                              nattrs_ = 0;

    [[nodiscard]] const Attribute& attr_at(size_t i) const noexcept {
        if (i < kInlineAttrs) return attrs_inline_[i];
        return attrs_spill_[i - kInlineAttrs];
    }
    Attribute& attr_at_mut(size_t i) noexcept {
        if (i < kInlineAttrs) return attrs_inline_[i];
        return attrs_spill_[i - kInlineAttrs];
    }
    void push_attr(Attribute a) {
        if (nattrs_ < kInlineAttrs) {
            attrs_inline_[nattrs_] = a;
        } else {
            attrs_spill_.push_back(a);
        }
        ++nattrs_;
    }
    void clear_attrs() noexcept {
        nattrs_ = 0;
        attrs_spill_.clear();
    }

    // ── Input buffer ─────────────────────────────────────────────────────────

    const char* data_ = nullptr;
    size_t      len_  = 0;
    size_t      pos_  = 0;         // current read position

    // ── Parser state ─────────────────────────────────────────────────────────

    XmlEvent    event_     = XmlEvent::StartDocument;
    bool        done_      = false;   // true after we return false from next()
    bool        started_   = false;   // true once next() has been called once

    /// When a self-closing tag <foo/> is scanned, we fire StartElement on
    /// one next() call and need to fire the synthetic EndElement on the next.
    /// pending_self_close_ holds the element name during that one-step gap.
    bool             pending_self_close_ = false;
    std::string_view pending_sc_name_;

    // ── Current event payload ─────────────────────────────────────────────

    std::string_view name_;       // element name (Start/End)
    std::string_view text_;       // text/cdata/comment content
    std::string_view pi_target_;  // PI target name
    std::string_view pi_data_;    // PI data string

    // ── Position tracking ─────────────────────────────────────────────────

    size_t line_      = 1;   // current line as we scan (1-based)
    size_t col_       = 1;   // current column (1-based)

    size_t event_line_ = 1;
    size_t event_col_  = 1;

    // ── Depth ────────────────────────────────────────────────────────────────

    size_t depth_ = 0;

    // ── Internal scanning helpers ─────────────────────────────────────────

    // Advance position by one character and update line/col.
    void advance_pos() noexcept {
        if (pos_ < len_) {
            if (data_[pos_] == '\n') { ++line_; col_ = 1; }
            else                     { ++col_; }
            ++pos_;
        }
    }

    // Advance by n bytes (for spans where we already know there are no newlines,
    // or we do not need fine-grained line tracking inside them).
    void advance_pos_n(size_t n) noexcept {
        // Update line/col precisely
        for (size_t i = 0; i < n && pos_ < len_; ++i)
            advance_pos();
    }

    // Set pos_ to `new_pos`, updating line/col incrementally.
    // Used after a SIMD scan that jumped ahead.
    void jump_to(size_t new_pos) noexcept {
        while (pos_ < new_pos && pos_ < len_)
            advance_pos();
    }

    // ── Scanning sub-routines ─────────────────────────────────────────────

    void skip_whitespace() noexcept {
        size_t np = skip_whitespace_fast(data_, pos_, len_);
        jump_to(np);
    }

    // Read an XML name starting at current pos_; returns the name view.
    std::string_view read_name() noexcept {
        size_t start = pos_;
        size_t end   = read_name_fast(data_, pos_, len_);
        jump_to(end);
        return std::string_view(data_ + start, end - start);
    }

    // Find the next occurrence of `ch` from pos_, return its position or len_.
    size_t find_char(char ch) const noexcept {
        return find_char_fast(data_, pos_, len_, ch);
    }

    // Require that data_[pos_] == expected; advance past it.
    // Throws ParseError otherwise.
    void expect(char expected) {
        if (pos_ >= len_ || data_[pos_] != expected) {
            throw ParseError(
                std::string("Expected '") + expected + "' at offset " +
                std::to_string(pos_), pos_);
        }
        advance_pos();
    }

    // ── Top-level event parsers ───────────────────────────────────────────

    // Called from next(); returns true if a real event was produced.
    bool parse_next_event();

    // Parse the markup starting after '<' has already been consumed.
    // Returns true if it produced a user-visible event.
    bool parse_markup();

    // Parse a start or self-closing tag (pos_ is past the '<').
    // Returns true (always produces at least one event).
    bool parse_start_tag();

    // Parse an end tag (pos_ is past '</').
    // Returns true.
    bool parse_end_tag();

    // Parse a comment (pos_ is past '<!--').
    bool parse_comment();

    // Parse a CDATA section (pos_ is past '<![CDATA[').
    bool parse_cdata();

    // Parse a PI (pos_ is past '<?').
    bool parse_pi();

    // Skip a DOCTYPE declaration (pos_ is past '<!DOCTYPE').
    void skip_doctype();

    // Parse text content up to the next '<' or end-of-input.
    // Returns true if non-empty.
    bool parse_text();
};

// =============================================================================
// Inline / template implementation
// =============================================================================

inline bool XmlReader::next() {
    if (done_) return false;

    // Emit the synthetic StartDocument exactly once, before anything else.
    if (!started_) {
        started_ = true;
        event_      = XmlEvent::StartDocument;
        event_line_ = 1;
        event_col_  = 1;
        name_ = text_ = pi_target_ = pi_data_ = {};
        clear_attrs();
        return true;
    }

    // Deliver the pending synthetic EndElement for self-closing tags.
    // depth_ was NOT incremented when the StartElement was issued for a
    // self-closing tag, so we must NOT decrement it here either.
    if (pending_self_close_) {
        pending_self_close_ = false;
        event_      = XmlEvent::EndElement;
        name_       = pending_sc_name_;
        text_       = {};
        clear_attrs();
        return true;
    }

    // Main parse loop: keep scanning until we produce a user-visible event
    // or exhaust the input.
    while (pos_ < len_) {
        if (parse_next_event()) return true;
    }

    // Input exhausted.
    done_  = true;
    event_ = XmlEvent::EndDocument;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_next_event() {
    // Record position of event start for line/column reporting.
    event_line_ = line_;
    event_col_  = col_;

    if (data_[pos_] == '<') {
        advance_pos();  // consume '<'
        return parse_markup();
    }

    // Text content
    return parse_text();
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_markup() {
    if (pos_ >= len_)
        throw ParseError("Unexpected end of input after '<'", pos_ - 1);

    char ch = data_[pos_];

    // ── End tag: </name> ──────────────────────────────────────────────────
    if (ch == '/') {
        advance_pos();
        return parse_end_tag();
    }

    // ── Comment: <!-- ─────────────────────────────────────────────────────
    if (ch == '!' && pos_ + 2 < len_ &&
        data_[pos_ + 1] == '-' && data_[pos_ + 2] == '-') {
        advance_pos_n(3);  // skip '!--'
        return parse_comment();
    }

    // ── CDATA: <![CDATA[ ──────────────────────────────────────────────────
    if (ch == '!' && pos_ + 7 < len_ &&
        std::memcmp(data_ + pos_ + 1, "[CDATA[", 7) == 0) {
        advance_pos_n(8);  // skip '![CDATA['
        return parse_cdata();
    }

    // ── DOCTYPE: <!DOCTYPE ────────────────────────────────────────────────
    if (ch == '!' && pos_ + 7 < len_ &&
        (std::memcmp(data_ + pos_ + 1, "DOCTYPE", 7) == 0 ||
         std::memcmp(data_ + pos_ + 1, "doctype", 7) == 0)) {
        advance_pos_n(8);  // skip '!DOCTYPE'
        skip_doctype();
        return false;  // DOCTYPE is invisible to the caller
    }

    // ── Processing instruction or XML declaration: <? ─────────────────────
    if (ch == '?') {
        advance_pos();  // skip '?'
        return parse_pi();
    }

    // ── Start tag or self-closing: <name ──────────────────────────────────
    return parse_start_tag();
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_start_tag() {
    skip_whitespace();

    std::string_view tag_name = read_name();

    if (tag_name.empty())
        throw ParseError("Expected element name", pos_);

    // ── Parse attributes ──────────────────────────────────────────────────
    clear_attrs();
    skip_whitespace();

    while (pos_ < len_ && data_[pos_] != '>' && data_[pos_] != '/') {
        // Attribute name
        std::string_view aname = read_name();
        if (aname.empty()) break;

        skip_whitespace();

        std::string_view aval;
        if (pos_ < len_ && data_[pos_] == '=') {
            advance_pos();  // skip '='
            skip_whitespace();

            if (pos_ < len_ && (data_[pos_] == '"' || data_[pos_] == '\'')) {
                char quote = data_[pos_];
                advance_pos();  // skip opening quote
                size_t val_start = pos_;
                size_t val_end   = skip_attr_value(data_, pos_, len_, quote);
                aval = std::string_view(data_ + val_start, val_end - val_start);
                jump_to(val_end);
                if (pos_ < len_ && data_[pos_] == quote)
                    advance_pos();  // skip closing quote
            }
        }

        push_attr({aname, aval});
        skip_whitespace();
    }

    // ── Check self-closing ────────────────────────────────────────────────
    bool self_closing = false;
    if (pos_ < len_ && data_[pos_] == '/') {
        self_closing = true;
        advance_pos();  // skip '/'
    }
    if (pos_ < len_ && data_[pos_] == '>') {
        advance_pos();  // skip '>'
    } else {
        throw ParseError("Expected '>' in start tag", pos_);
    }

    event_ = XmlEvent::StartElement;
    name_  = tag_name;
    text_  = {};

    if (self_closing) {
        // Will fire EndElement on the next next() call.
        pending_self_close_ = true;
        pending_sc_name_    = tag_name;
        // depth does NOT increase for self-closing: net effect is zero.
    } else {
        ++depth_;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_end_tag() {
    skip_whitespace();
    std::string_view tag_name = read_name();

    if (tag_name.empty())
        throw ParseError("Expected tag name in end tag", pos_);

    // Skip optional whitespace then require '>'
    skip_whitespace();
    if (pos_ < len_ && data_[pos_] == '>') {
        advance_pos();
    } else {
        throw ParseError("Expected '>' in end tag", pos_);
    }

    event_ = XmlEvent::EndElement;
    name_  = tag_name;
    text_  = {};
    clear_attrs();

    if (depth_ > 0) --depth_;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_comment() {
    size_t content_start = pos_;
    // Find "-->"
    size_t end = find_comment_end(data_, pos_, len_);
    if (end >= len_) {
        jump_to(len_);
        // Unterminated comment – silently ignore
        return false;
    }
    text_  = std::string_view(data_ + content_start, end - content_start);
    jump_to(end + 3);  // skip "-->"
    event_ = XmlEvent::Comment;
    name_  = {};
    clear_attrs();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_cdata() {
    size_t content_start = pos_;
    size_t end = find_cdata_end(data_, pos_, len_);
    if (end >= len_) {
        jump_to(len_);
        return false;
    }
    text_  = std::string_view(data_ + content_start, end - content_start);
    jump_to(end + 3);  // skip "]]>"
    event_ = XmlEvent::CData;
    name_  = {};
    clear_attrs();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_pi() {
    skip_whitespace();
    size_t target_start = pos_;
    std::string_view target = read_name();

    // Find "?>"
    size_t pi_end = find_pi_end(data_, pos_, len_);
    if (pi_end >= len_) {
        jump_to(len_);
        return false;
    }

    // Trim leading whitespace from PI data
    size_t data_start = pos_;
    while (data_start < pi_end && is_whitespace(data_[data_start]))
        ++data_start;

    std::string_view pi_content(data_ + data_start, pi_end - data_start);
    jump_to(pi_end + 2);  // skip "?>"

    if (target == "xml" || target == "XML") {
        // XML declaration — extract version/encoding/standalone attributes
        // and store them accessibly via name() (version), text() (encoding),
        // pi_data_ (standalone).
        auto extract = [&](const char* attr_name) -> std::string_view {
            // search in the full span from target onwards
            std::string_view full(data_ + target_start,
                                  pi_end - target_start);
            auto p = full.find(attr_name);
            if (p == std::string_view::npos) return {};
            p = full.find('=', p);
            if (p == std::string_view::npos) return {};
            ++p;
            while (p < full.size() &&
                   (full[p] == ' ' || full[p] == '"' || full[p] == '\''))
                ++p;
            size_t s = p;
            while (p < full.size() && full[p] != '"' && full[p] != '\'') ++p;
            return full.substr(s, p - s);
        };

        event_     = XmlEvent::XmlDeclaration;
        name_      = extract("version");    // version via name()
        text_      = extract("encoding");   // encoding via text()
        pi_data_   = extract("standalone"); // standalone via value() / pi_data_
        pi_target_ = target;
        clear_attrs();
        return true;
    }

    event_     = XmlEvent::ProcessingInstruction;
    pi_target_ = target;
    name_      = target;       // name() == target for PI (convenience)
    pi_data_   = pi_content;
    text_      = pi_content;   // text() also returns the data
    clear_attrs();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline void XmlReader::skip_doctype() {
    // pos_ is right after '<!DOCTYPE'; scan to the closing '>', respecting
    // internal subset brackets.
    int bracket_depth = 0;
    while (pos_ < len_) {
        char c = data_[pos_];
        advance_pos();
        if (c == '[')       { ++bracket_depth; }
        else if (c == ']')  { if (bracket_depth > 0) --bracket_depth; }
        else if (c == '>' && bracket_depth == 0) { break; }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

inline bool XmlReader::parse_text() {
    size_t text_start = pos_;
    // Stop at '<' (or end of input).  SIMD-accelerated.
    size_t text_end = skip_text_turbo(data_, pos_, len_);
    if (text_end == text_start) {
        // We're right at a '<' (or end); no text here.
        return false;
    }
    text_  = std::string_view(data_ + text_start, text_end - text_start);
    jump_to(text_end);
    event_ = XmlEvent::Text;
    name_  = {};
    clear_attrs();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

inline std::string XmlReader::read_element_text() {
    // Caller must be on a StartElement.
    assert(event_ == XmlEvent::StartElement);

    std::string result;
    int open = 1;  // we are inside depth of current element

    // We may have a pending self-close – that means the element body is empty.
    // depth_ was not incremented for the self-closing tag, so don't decrement.
    if (pending_self_close_) {
        pending_self_close_ = false;
        return result;
    }

    while (next()) {
        switch (event_) {
            case XmlEvent::StartElement:
                ++open;
                break;
            case XmlEvent::EndElement:
                --open;
                if (open == 0) return result;  // consumed matching end tag
                break;
            case XmlEvent::Text:
            case XmlEvent::CData:
                if (open == 1) result += text_;
                break;
            default:
                break;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────

inline void XmlReader::skip_element() {
    // Caller must be on a StartElement.
    assert(event_ == XmlEvent::StartElement);

    // Self-closing element – nothing inside.
    // depth_ was not incremented for the self-closing tag, so don't decrement.
    if (pending_self_close_) {
        pending_self_close_ = false;
        return;
    }

    int open = 1;
    while (next()) {
        if (event_ == XmlEvent::StartElement) {
            ++open;
        } else if (event_ == XmlEvent::EndElement) {
            --open;
            if (open == 0) return;
        }
    }
}

} // namespace parshred
