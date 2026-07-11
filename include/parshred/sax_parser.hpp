#pragma once
/// @file sax_parser.hpp
/// @brief SAX (event-driven) streaming XML parser.

#include <parshred/common.hpp>
#include <parshred/mmap_reader.hpp>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace parshred {

/// Event-driven (SAX-style) XML parser.
///
/// Register callbacks, then call parse() or parse_file().
/// Callbacks receive zero-copy string_views into the original input.
///
/// Example:
/// @code
///   SaxParser parser;
///   parser.on_start_element([](std::string_view name, std::span<const Attribute> attrs) {
///       std::cout << "<" << name << ">\n";
///   });
///   parser.parse_file("data.xml");
/// @endcode
class SaxParser {
public:
    // ── Callback types ────────────────────────────────────────────────
    using StartElementCb  = std::function<void(std::string_view name,
                                               std::span<const Attribute> attrs)>;
    using EndElementCb    = std::function<void(std::string_view name)>;
    using TextCb          = std::function<void(std::string_view text)>;
    using CommentCb       = std::function<void(std::string_view text)>;
    using PICb            = std::function<void(std::string_view target,
                                               std::string_view data)>;
    using CDataCb         = std::function<void(std::string_view text)>;
    using XmlDeclCb       = std::function<void(std::string_view version,
                                               std::string_view encoding,
                                               std::string_view standalone)>;

    SaxParser() = default;

    // ── Callback registration ─────────────────────────────────────────
    void on_start_element(StartElementCb cb)  { start_element_cb_ = std::move(cb); }
    void on_end_element(EndElementCb cb)      { end_element_cb_   = std::move(cb); }
    void on_text(TextCb cb)                   { text_cb_          = std::move(cb); }
    void on_comment(CommentCb cb)             { comment_cb_       = std::move(cb); }
    void on_processing_instruction(PICb cb)   { pi_cb_            = std::move(cb); }
    void on_cdata(CDataCb cb)                 { cdata_cb_         = std::move(cb); }
    void on_xml_declaration(XmlDeclCb cb)     { xml_decl_cb_      = std::move(cb); }

    // ── Parsing ───────────────────────────────────────────────────────
    /// Parse from a memory span.
    void parse(std::span<const char> input);

    /// Parse from a file (uses mmap internally).
    void parse_file(const std::string& path);

    /// Parse from a string.
    void parse_string(std::string_view str);

    // ── Security limits ───────────────────────────────────────────────
    void set_max_depth(size_t depth)             { max_depth_ = depth; }
    void set_max_entity_expansions(size_t n)     { max_entity_expansions_ = n; }
    void set_max_attribute_count(size_t n)       { max_attribute_count_ = n; }

    [[nodiscard]] size_t max_depth() const noexcept             { return max_depth_; }
    [[nodiscard]] size_t max_entity_expansions() const noexcept { return max_entity_expansions_; }

    // ── Statistics ────────────────────────────────────────────────────
    struct Stats {
        size_t elements      = 0;
        size_t attributes    = 0;
        size_t text_nodes    = 0;
        size_t comments      = 0;
        size_t cdata_nodes   = 0;
        size_t bytes_parsed  = 0;
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

private:
    // Callbacks
    StartElementCb  start_element_cb_;
    EndElementCb    end_element_cb_;
    TextCb          text_cb_;
    CommentCb       comment_cb_;
    PICb            pi_cb_;
    CDataCb         cdata_cb_;
    XmlDeclCb       xml_decl_cb_;

    // Security limits
    size_t max_depth_             = DEFAULT_MAX_DEPTH;
    size_t max_entity_expansions_ = DEFAULT_MAX_ENTITY_EXPANSIONS;
    size_t max_attribute_count_   = DEFAULT_MAX_ATTRIBUTE_COUNT;

    // Runtime state
    Stats  stats_;
    size_t current_depth_        = 0;
    size_t entity_expansion_count_ = 0;

    // Attribute scratch buffer (reused across elements)
    std::vector<Attribute> attrs_scratch_;

    // Internal parsing helpers
    void do_parse(std::span<const char> input);

    /// Expand the 5 predefined XML entities in-place.
    /// Returns the expanded string (may allocate if entities are present).
    [[nodiscard]] std::string expand_entities(std::string_view text);
};

} // namespace parshred
