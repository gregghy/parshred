#pragma once
/// @file fast_sax.hpp
/// @brief Fused single-pass SAX parser — no intermediate token vector.
///
/// This is the high-performance path. Instead of:
///   SIMD scan → StructuralIndex → Tokenizer → Token vector → SAX
///
/// We do:
///   raw input → direct single-pass parse → SAX callbacks
///
/// The parser scans the input directly using SIMD-accelerated primitives
/// (skip_whitespace, find_char, skip_text) without building any
/// intermediate data structures.
///
/// Two modes:
///   - ParseMode::Normal   — expand entities, check depth, collect stats
///   - ParseMode::Turbo    — skip entities, no depth check, no stats
///                           (equivalent to RapidXML's parse_fastest)

#include <parshred/common.hpp>
#include <parshred/lookup_tables.hpp>
#include <parshred/mmap_reader.hpp>

#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace parshred {

/// Parse mode flags — controls which processing is enabled.
enum class ParseMode : uint32_t {
    Normal = 0,          // Full conformant parsing
    Turbo  = 1,          // Skip entity expansion, depth checking, stats
};

/// Callback interface for template-based parsing (zero overhead).
///
/// Users inherit from this and override the methods they care about.
/// The compiler inlines the callbacks directly into the parse loop,
/// eliminating all std::function overhead.
///
/// Example:
/// @code
///   struct MyHandler : parshred::SaxHandler {
///       size_t count = 0;
///       void on_start_element(std::string_view name,
///                             const Attribute* attrs, size_t nattrs) override {
///           ++count;
///       }
///   };
///   MyHandler h;
///   parshred::fast_parse(data, len, h);
/// @endcode
struct SaxHandler {
    virtual ~SaxHandler() = default;
    virtual void on_start_element(std::string_view name,
                                  const Attribute* attrs, size_t nattrs) {}
    virtual void on_end_element(std::string_view name) {}
    virtual void on_text(std::string_view text) {}
    virtual void on_cdata(std::string_view text) {}
    virtual void on_comment(std::string_view text) {}
    virtual void on_processing_instruction(std::string_view target,
                                           std::string_view data) {}
    virtual void on_xml_declaration(std::string_view version,
                                    std::string_view encoding,
                                    std::string_view standalone) {}
};

/// Null handler — does nothing, used for parse-only benchmarks.
struct NullHandler final : SaxHandler {};

/// Counting handler — just counts events.
struct CountingHandler final : SaxHandler {
    size_t elements = 0;
    size_t end_tags = 0;
    size_t text_nodes = 0;
    size_t attributes = 0;

    void on_start_element(std::string_view,
                          const Attribute* attrs, size_t nattrs) override {
        ++elements;
        attributes += nattrs;
    }
    void on_end_element(std::string_view) override { ++end_tags; }
    void on_text(std::string_view) override { ++text_nodes; }
};

/// The fused single-pass SAX parser.
///
/// Template parameter `Mode` selects compile-time optimizations.
/// Template parameter `Handler` allows devirtualization when the
/// concrete type is known at compile time.
template<ParseMode Mode = ParseMode::Normal>
class FastSaxParser {
public:
    FastSaxParser() = default;

    /// Parse from a memory span, calling handler methods for events.
    /// Resets parser state (depth, stats) before parsing.
    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    /// Parse a chunk without resetting parser state.
    /// Used by the chunked pipeline to maintain depth/stats across chunks.
    template<typename Handler>
    void parse_chunk(const char* data, size_t len, Handler& handler);

    /// Parse from a span.
    template<typename Handler>
    void parse(std::span<const char> input, Handler& handler) {
        parse(input.data(), input.size(), handler);
    }

    /// Parse from a string or string_view.
    /// Uses a separate overload name to avoid ambiguity with std::string.
    template<typename Handler>
    void parse_string(std::string_view input, Handler& handler) {
        parse(input.data(), input.size(), handler);
    }

    /// Parse from a file.
    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler) {
        MmapReader reader;
        reader.open(path);
        auto d = reader.data();
        parse(d.data(), d.size(), handler);
    }

    // ── Security limits (only effective in Normal mode) ──────────────
    void set_max_depth(size_t d)             { max_depth_ = d; }
    void set_max_entity_expansions(size_t n) { max_entity_expansions_ = n; }
    void set_max_attribute_count(size_t n)   { max_attribute_count_ = n; }

    // ── Statistics (only collected in Normal mode) ────────────────────
    struct Stats {
        size_t elements     = 0;
        size_t attributes   = 0;
        size_t text_nodes   = 0;
        size_t comments     = 0;
        size_t cdata_nodes  = 0;
        size_t bytes_parsed = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    // Security limits
    size_t max_depth_             = DEFAULT_MAX_DEPTH;
    size_t max_entity_expansions_ = DEFAULT_MAX_ENTITY_EXPANSIONS;
    size_t max_attribute_count_   = DEFAULT_MAX_ATTRIBUTE_COUNT;

    // Runtime state
    Stats  stats_{};
    size_t depth_ = 0;
    size_t entity_count_ = 0;

    // Attribute scratch buffer (reused, pre-reserved)
    Attribute attrs_buf_[64];  // inline storage for common case
    std::vector<Attribute> attrs_overflow_;  // fallback for >64 attrs

    // Internal: expand entities in text (Normal mode only)
    std::string entity_buf_;
    void expand_and_emit_text(std::string_view text, SaxHandler& handler);
};

// ── Convenience free functions ──────────────────────────────────────

/// Parse with turbo mode — maximum speed, no entity expansion.
template<typename Handler>
inline void fast_parse_turbo(const char* data, size_t len, Handler& handler) {
    FastSaxParser<ParseMode::Turbo> parser;
    parser.parse(data, len, handler);
}

/// Parse with turbo mode from string_view.
template<typename Handler>
inline void fast_parse_turbo(std::string_view input, Handler& handler) {
    fast_parse_turbo(input.data(), input.size(), handler);
}

/// Parse with normal mode.
template<typename Handler>
inline void fast_parse(const char* data, size_t len, Handler& handler) {
    FastSaxParser<ParseMode::Normal> parser;
    parser.parse(data, len, handler);
}

/// Parse with normal mode from string_view.
template<typename Handler>
inline void fast_parse(std::string_view input, Handler& handler) {
    fast_parse(input.data(), input.size(), handler);
}

// ── Compatibility wrapper ───────────────────────────────────────────

/// std::function-based API for easy migration from SaxParser.
/// Slightly slower due to std::function indirection, but still uses
/// the fused single-pass architecture.
class FastSaxParserEasy {
public:
    using StartElementCb = std::function<void(std::string_view name,
                                              std::span<const Attribute> attrs)>;
    using EndElementCb   = std::function<void(std::string_view name)>;
    using TextCb         = std::function<void(std::string_view text)>;
    using CommentCb      = std::function<void(std::string_view text)>;
    using CDataCb        = std::function<void(std::string_view text)>;

    void on_start_element(StartElementCb cb) { start_cb_ = std::move(cb); }
    void on_end_element(EndElementCb cb)     { end_cb_ = std::move(cb); }
    void on_text(TextCb cb)                  { text_cb_ = std::move(cb); }
    void on_comment(CommentCb cb)            { comment_cb_ = std::move(cb); }
    void on_cdata(CDataCb cb)               { cdata_cb_ = std::move(cb); }

    void set_max_depth(size_t d)             { max_depth_ = d; }
    void set_max_entity_expansions(size_t n) { max_entities_ = n; }
    void set_max_attribute_count(size_t n)   { max_attrs_ = n; }

    void parse_string(std::string_view input);
    void parse_file(const std::string& path);

    struct Stats {
        size_t elements     = 0;
        size_t attributes   = 0;
        size_t text_nodes   = 0;
        size_t comments     = 0;
        size_t cdata_nodes  = 0;
        size_t bytes_parsed = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    StartElementCb start_cb_;
    EndElementCb   end_cb_;
    TextCb         text_cb_;
    CommentCb      comment_cb_;
    CDataCb        cdata_cb_;

    size_t max_depth_    = DEFAULT_MAX_DEPTH;
    size_t max_entities_ = DEFAULT_MAX_ENTITY_EXPANSIONS;
    size_t max_attrs_    = DEFAULT_MAX_ATTRIBUTE_COUNT;
    Stats  stats_{};
};

} // namespace parshred

// Include the template implementation
#include <parshred/fast_sax_impl.hpp>
