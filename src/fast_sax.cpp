/// @file fast_sax.cpp
/// @brief Fused single-pass SAX parser implementation.
///
/// This is the performance-critical core.  The parser walks the raw input
/// directly, using SIMD-accelerated scanning primitives and lookup tables
/// instead of building any intermediate token or structural-index vector.
///
/// The key insight (borrowed from RapidXML but taken further with SIMD):
///   1. Text between tags is the majority of XML — skip it at 5+ GB/s
///      using SIMD to find the next '<' or '&'.
///   2. Tag names and attribute names are short — the scalar lookup table
///      path (one indexed load per byte) is branch-prediction-friendly.
///   3. Attribute values can be long — use SIMD find_char for the closing
///      quote, scanning 32 bytes at a time.
///   4. Eliminate ALL intermediate allocations. Callbacks receive
///      string_views directly into the mmap'd input buffer.

#include <parshred/fast_sax.hpp>
#include <parshred/simd_utils.hpp>
#include <parshred/lookup_tables.hpp>

#include <cstring>

namespace parshred {

// ── Entity expansion (Normal mode only) ─────────────────────────────

template<>
void FastSaxParser<ParseMode::Normal>::expand_and_emit_text(
    std::string_view text, SaxHandler& handler)
{
    // Fast path: no '&' at all — emit directly, zero copy
    if (text.find('&') == std::string_view::npos) {
        handler.on_text(text);
        return;
    }

    entity_buf_.clear();
    entity_buf_.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            entity_buf_ += text[i];
            ++i;
            continue;
        }

        size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos) {
            entity_buf_ += text[i];
            ++i;
            continue;
        }

        if (++entity_count_ > max_entity_expansions_) {
            throw SecurityError("Entity expansion limit exceeded", 0);
        }

        std::string_view ent = text.substr(i + 1, semi - i - 1);
        if (ent == "lt")        entity_buf_ += '<';
        else if (ent == "gt")   entity_buf_ += '>';
        else if (ent == "amp")  entity_buf_ += '&';
        else if (ent == "apos") entity_buf_ += '\'';
        else if (ent == "quot") entity_buf_ += '"';
        else if (ent.size() > 1 && ent[0] == '#') {
            unsigned long cp = 0;
            if (ent[1] == 'x' || ent[1] == 'X') {
                for (size_t j = 2; j < ent.size(); ++j) {
                    char c = ent[j];
                    cp *= 16;
                    if (c >= '0' && c <= '9')      cp += c - '0';
                    else if (c >= 'a' && c <= 'f')  cp += 10 + c - 'a';
                    else if (c >= 'A' && c <= 'F')  cp += 10 + c - 'A';
                }
            } else {
                for (size_t j = 1; j < ent.size(); ++j)
                    cp = cp * 10 + (ent[j] - '0');
            }
            if (cp < 0x80) {
                entity_buf_ += static_cast<char>(cp);
            } else if (cp < 0x800) {
                entity_buf_ += static_cast<char>(0xC0 | (cp >> 6));
                entity_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                entity_buf_ += static_cast<char>(0xE0 | (cp >> 12));
                entity_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                entity_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x110000) {
                entity_buf_ += static_cast<char>(0xF0 | (cp >> 18));
                entity_buf_ += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                entity_buf_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                entity_buf_ += static_cast<char>(0x80 | (cp & 0x3F));
            }
        } else {
            // Unknown entity — pass through
            entity_buf_.append(text.data() + i, semi - i + 1);
        }
        i = semi + 1;
    }

    handler.on_text(entity_buf_);
}

template<>
void FastSaxParser<ParseMode::Turbo>::expand_and_emit_text(
    std::string_view, SaxHandler&) {
    // Never called in turbo mode
}

// (The core parse template is in fast_sax_impl.hpp, included from fast_sax.hpp)

// ── FastSaxParserEasy (std::function wrapper) ────────────────────────

namespace {

struct EasyBridge final : SaxHandler {
    FastSaxParserEasy::Stats& stats;
    const std::function<void(std::string_view, std::span<const Attribute>)>* start_cb;
    const std::function<void(std::string_view)>* end_cb;
    const std::function<void(std::string_view)>* text_cb;
    const std::function<void(std::string_view)>* comment_cb;
    const std::function<void(std::string_view)>* cdata_cb;

    EasyBridge(FastSaxParserEasy::Stats& s,
               const std::function<void(std::string_view, std::span<const Attribute>)>* se,
               const std::function<void(std::string_view)>* ee,
               const std::function<void(std::string_view)>* te,
               const std::function<void(std::string_view)>* ce,
               const std::function<void(std::string_view)>* cd)
        : stats(s), start_cb(se), end_cb(ee), text_cb(te), comment_cb(ce), cdata_cb(cd) {}

    void on_start_element(std::string_view name,
                          const Attribute* attrs, size_t nattrs) override {
        ++stats.elements;
        stats.attributes += nattrs;
        if (*start_cb) (*start_cb)(name, std::span<const Attribute>(attrs, nattrs));
    }
    void on_end_element(std::string_view name) override {
        if (*end_cb) (*end_cb)(name);
    }
    void on_text(std::string_view text) override {
        ++stats.text_nodes;
        if (*text_cb) (*text_cb)(text);
    }
    void on_comment(std::string_view text) override {
        ++stats.comments;
        if (*comment_cb) (*comment_cb)(text);
    }
    void on_cdata(std::string_view text) override {
        ++stats.cdata_nodes;
        if (*cdata_cb) (*cdata_cb)(text);
    }
};

} // anonymous namespace

void FastSaxParserEasy::parse_string(std::string_view input) {
    stats_ = {};
    stats_.bytes_parsed = input.size();

    EasyBridge bridge(stats_, &start_cb_, &end_cb_, &text_cb_, &comment_cb_, &cdata_cb_);

    FastSaxParser<ParseMode::Normal> parser;
    parser.set_max_depth(max_depth_);
    parser.set_max_entity_expansions(max_entities_);
    parser.set_max_attribute_count(max_attrs_);
    parser.parse(input.data(), input.size(), bridge);
}

void FastSaxParserEasy::parse_file(const std::string& path) {
    MmapReader reader;
    reader.open(path);
    auto d = reader.data();
    stats_ = {};
    stats_.bytes_parsed = d.size();

    EasyBridge bridge(stats_, &start_cb_, &end_cb_, &text_cb_, &comment_cb_, &cdata_cb_);

    FastSaxParser<ParseMode::Normal> parser;
    parser.set_max_depth(max_depth_);
    parser.set_max_entity_expansions(max_entities_);
    parser.set_max_attribute_count(max_attrs_);
    parser.parse(d.data(), d.size(), bridge);
}

} // namespace parshred
