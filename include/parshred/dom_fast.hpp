#pragma once
/// @file dom_fast.hpp
/// @brief Ultra-compact DOM designed to beat RapidXML at all sizes.
///
/// Key insight: RapidXML is fast because it writes minimal data per node.
/// This compact DOM uses:
///   - 32-byte nodes (2 nodes per cache line vs our 80-byte nodes)
///   - Offsets instead of pointers (saves 4 bytes per link on 64-bit)
///   - Flat array storage (perfect spatial locality, no pointer chasing)
///   - No parent pointer (saves 8 bytes, rarely needed for traversal)
///
/// The flat layout means: parsing writes sequentially to memory,
/// which is optimal for modern CPUs with write-combining buffers.

#include <parshred/common.hpp>
#include <parshred/lookup_tables.hpp>
#include <parshred/simd_utils.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace parshred {

// ── Compact Node ─────────────────────────────────────────────────────

/// 32-byte compact node — 2 nodes per cache line!
///
/// Designed for maximum parse speed. Uses string_view (pointer + length)
/// directly, with sibling encoded as offset from current node index.
struct CompactNode {
    std::string_view name;          // 16 bytes: element/attr name
    uint32_t         first_child;   //  4 bytes: index of first child (0 = none)
    uint32_t         next_sibling;  //  4 bytes: index of next sibling (0 = none)
    uint32_t         first_attr;    //  4 bytes: index of first attribute (0 = none)
    uint8_t          type;          //  1 byte: NodeType
    uint8_t          flags;         //  1 byte
    uint16_t         _pad;          //  2 bytes
    // Total: 32 bytes exactly
};

static_assert(sizeof(CompactNode) == 32, "CompactNode must be 32 bytes");

/// Attribute in the compact DOM — even lighter (24 bytes).
struct CompactAttr {
    std::string_view name;          // 16 bytes
    std::string_view value;         // 16 bytes  
    uint32_t         next;          //  4 bytes: index of next attr (0 = none)
    uint32_t         _pad;          //  4 bytes
    // Total: 40 bytes... too big. Let's merge into one type.
};

// Actually, let's use a unified node that serves both elements and attrs:

/// 32-byte unified node for the compact DOM.
/// For elements: name = tag name, value is unused (or holds text for text nodes)
/// For attributes: name = attr name, value stored in separate values array
struct FastNode {
    const char*      name_ptr;      //  8 bytes: pointer into source buffer
    uint32_t         name_len;      //  4 bytes: length of name
    uint32_t         first_child;   //  4 bytes: index (0 = none)
    uint32_t         next_sibling;  //  4 bytes: index (0 = none)
    uint32_t         first_attr;    //  4 bytes: index (0 = none)  
    uint32_t         value_offset;  //  4 bytes: offset into values buffer
    uint16_t         value_len;     //  2 bytes: length of value
    uint8_t          type;          //  1 byte
    uint8_t          flags;         //  1 byte
    // Total: 32 bytes
};

static_assert(sizeof(FastNode) == 32, "FastNode must be 32 bytes");

// ── Fast DOM Result ──────────────────────────────────────────────────

/// The compact DOM tree — raw array of 32-byte nodes.
/// Uses malloc (no zero-init) for maximum allocation speed.
struct FastDom {
    FastNode*         nodes = nullptr;     // Raw node array (malloc'd)
    size_t            node_count = 0;      // Number of valid nodes
    size_t            capacity = 0;        // Allocated capacity
    std::vector<char> values;              // Text content (only in non-fastest mode)
    const char*       data_ptr = nullptr;  // Original input data
    uint32_t          root_idx = 0;        // Index of root element

    FastDom() = default;
    ~FastDom() { if (nodes) std::free(nodes); }
    FastDom(FastDom&& o) noexcept
        : nodes(o.nodes), node_count(o.node_count), capacity(o.capacity),
          values(std::move(o.values)), data_ptr(o.data_ptr), root_idx(o.root_idx) {
        o.nodes = nullptr; o.node_count = 0; o.capacity = 0;
    }
    FastDom& operator=(FastDom&& o) noexcept {
        if (this != &o) {
            if (nodes) std::free(nodes);
            nodes = o.nodes; node_count = o.node_count; capacity = o.capacity;
            values = std::move(o.values); data_ptr = o.data_ptr; root_idx = o.root_idx;
            o.nodes = nullptr; o.node_count = 0; o.capacity = 0;
        }
        return *this;
    }
    FastDom(const FastDom&) = delete;
    FastDom& operator=(const FastDom&) = delete;

    // ── Access ───────────────────────────────────────────────────────

    [[nodiscard]] const FastNode& root() const noexcept { return nodes[root_idx]; }

    [[nodiscard]] std::string_view name(const FastNode& n) const noexcept {
        return {n.name_ptr, n.name_len};
    }

    [[nodiscard]] std::string_view value(const FastNode& n) const noexcept {
        if (n.value_len == 0) return {};
        // Flag bit 0x01: value lives in the `values` buffer (text nodes in non-fastest mode)
        if (n.flags & 0x01) return {values.data() + n.value_offset, n.value_len};
        // Otherwise value_offset is relative to data_ptr (attributes in zero-copy mode)
        if (data_ptr) return {data_ptr + n.value_offset, n.value_len};
        return {values.data() + n.value_offset, n.value_len};
    }

    [[nodiscard]] const FastNode* first_child(const FastNode& n) const noexcept {
        return n.first_child ? &nodes[n.first_child] : nullptr;
    }

    [[nodiscard]] const FastNode* next_sibling(const FastNode& n) const noexcept {
        return n.next_sibling ? &nodes[n.next_sibling] : nullptr;
    }

    [[nodiscard]] const FastNode* first_attr(const FastNode& n) const noexcept {
        return n.first_attr ? &nodes[n.first_attr] : nullptr;
    }

    [[nodiscard]] std::string_view attr(const FastNode& elem, std::string_view attr_name) const noexcept {
        uint32_t idx = elem.first_attr;
        while (idx) {
            const auto& a = nodes[idx];
            if (name(a) == attr_name) return value(a);
            idx = a.next_sibling;
        }
        return {};
    }

    [[nodiscard]] size_t element_count() const noexcept {
        size_t count = 0;
        for (size_t i = 0; i < node_count; ++i) {
            if (nodes[i].type == 1) ++count;
        }
        return count;
    }
};

// ── Fast DOM Parser ──────────────────────────────────────────────────

/// Parse mode flags for the fast DOM parser.
inline constexpr unsigned FDOM_NO_TEXT    = 1u << 0;  // Skip text nodes
inline constexpr unsigned FDOM_NO_ATTRS   = 1u << 1;  // Skip attributes
inline constexpr unsigned FDOM_NO_COMMENT = 1u << 2;  // Skip comments
inline constexpr unsigned FDOM_FASTEST    = FDOM_NO_TEXT | FDOM_NO_COMMENT;

/// Parse XML into a compact flat DOM tree.
/// This is the speed-record attempt — targeting 3+ GB/s.
template<unsigned Flags = 0>
FastDom fast_dom_parse(const char* data, size_t len) {
    FastDom dom;
    
    // Pre-allocate: estimate 1 node per ~35 bytes
    // Use raw malloc — no zero-initialization! We write all fields before reading.
    size_t est = len / 35 + 64;
    dom.nodes = static_cast<FastNode*>(std::malloc(est * sizeof(FastNode)));
    if (!dom.nodes) throw std::bad_alloc();
    dom.capacity = est;
    if constexpr (!(Flags & FDOM_NO_TEXT)) {
        dom.values.reserve(len / 4);
    }

    dom.data_ptr = data;
    FastNode* nodes = dom.nodes;
    uint32_t node_count = 0;

    // Sentinel node at index 0 (represents "null"/document)
    nodes[node_count++] = FastNode{};  // index 0 = null sentinel
    
    // Document node at index 1
    nodes[node_count++] = FastNode{.type = 0};  // Document type

    // Track last child of each element for O(1) append
    // Use a small stack of "last child index" per depth level
    struct StackEntry {
        uint32_t node_idx;
        uint32_t last_child_idx;
    };
    // Use a fixed-size stack (max depth 256 — more than enough)
    StackEntry stack[256];
    int stack_top = 0;
    stack[0] = {1, 0};  // Document level

    // Ensure we have space for at least one more node
    auto ensure_capacity = [&]() __attribute__((always_inline)) {
        if (__builtin_expect(node_count >= est, 0)) {
            est *= 2;
            nodes = static_cast<FastNode*>(std::realloc(nodes, est * sizeof(FastNode)));
            if (!nodes) throw std::bad_alloc();
            dom.nodes = nodes;
            dom.capacity = est;
        }
    };

    size_t pos = 0;

    while (pos < len) {
        // ── Skip text content ────────────────────────────────────────
        size_t text_start = pos;
        pos = skip_text_turbo(data, pos, len);

        if constexpr (!(Flags & FDOM_NO_TEXT)) {
            if (pos > text_start) {
                ensure_capacity();
                uint32_t idx = node_count;
                nodes[node_count] = FastNode{};
                nodes[node_count].type = 2;  // Text
                nodes[node_count].flags = 0x01;  // Value in values buffer
                nodes[node_count].name_ptr = data + text_start;
                nodes[node_count].value_offset = static_cast<uint32_t>(dom.values.size());
                nodes[node_count].value_len = static_cast<uint16_t>(
                    std::min(pos - text_start, size_t(65535)));
                dom.values.insert(dom.values.end(),
                                  data + text_start, data + pos);
                ++node_count;

                auto& top = stack[stack_top];
                if (top.last_child_idx) {
                    nodes[top.last_child_idx].next_sibling = idx;
                } else {
                    nodes[top.node_idx].first_child = idx;
                }
                top.last_child_idx = idx;
            }
        }

        if (pos >= len) break;

        // ── At '<' ──────────────────────────────────────────────────
        ++pos;
        if (pos >= len) break;
        char next = data[pos];

        // ── End tag ──────────────────────────────────────────────────
        if (next == '/') {
            // Skip to '>'
            while (pos < len && data[pos] != '>') ++pos;
            if (pos < len) ++pos;
            if (stack_top > 0) --stack_top;
            continue;
        }

        // ── Comment ──────────────────────────────────────────────────
        if (next == '!' && pos + 2 < len && data[pos+1] == '-' && data[pos+2] == '-') {
            pos += 3;
            size_t end = find_comment_end(data, pos, len);
            pos = (end < len) ? end + 3 : len;
            continue;
        }

        // ── CDATA ────────────────────────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            std::memcmp(data + pos + 1, "[CDATA[", 7) == 0) {
            pos += 8;
            size_t end = find_cdata_end(data, pos, len);
            pos = (end < len) ? end + 3 : len;
            continue;
        }

        // ── DOCTYPE ──────────────────────────────────────────────────
        if (next == '!' && pos + 7 < len &&
            (std::memcmp(data + pos + 1, "DOCTYPE", 7) == 0 ||
             std::memcmp(data + pos + 1, "doctype", 7) == 0)) {
            int bd = 0;
            while (pos < len) {
                if (data[pos] == '[') ++bd;
                else if (data[pos] == ']' && bd > 0) --bd;
                else if (data[pos] == '>' && bd == 0) { ++pos; break; }
                ++pos;
            }
            continue;
        }

        // ── PI ───────────────────────────────────────────────────────
        if (next == '?') {
            size_t pi_end = find_pi_end(data, pos + 1, len);
            pos = (pi_end < len) ? pi_end + 2 : len;
            continue;
        }

        // ── Start tag ────────────────────────────────────────────────
        {
            size_t name_start = pos;
            if (__builtin_expect(pos < len && parshred::is_name_start(data[pos]), 1)) {
                ++pos;
                while (pos < len && parshred::is_name_char(data[pos])) ++pos;
            }
            size_t name_end = pos;

            if (__builtin_expect(name_end == name_start, 0)) {
                while (pos < len && data[pos] != '>') ++pos;
                if (pos < len) ++pos;
                continue;
            }

            // Create element node
            ensure_capacity();
            uint32_t elem_idx = node_count;
            nodes[node_count] = FastNode{};
            nodes[node_count].type = 1;  // Element
            nodes[node_count].name_ptr = data + name_start;
            nodes[node_count].name_len = static_cast<uint32_t>(name_end - name_start);
            ++node_count;

            // Link to parent's children
            auto& top = stack[stack_top];
            if (top.last_child_idx) {
                nodes[top.last_child_idx].next_sibling = elem_idx;
            } else {
                nodes[top.node_idx].first_child = elem_idx;
            }
            top.last_child_idx = elem_idx;

            // Set root if first element under document
            if (stack_top == 0 && dom.root_idx == 0) {
                dom.root_idx = elem_idx;
            }

            // ── Parse attributes ─────────────────────────────────────
            while (pos < len && parshred::is_whitespace(data[pos])) ++pos;

            uint32_t last_attr_idx_local = 0;

            while (pos < len && data[pos] != '>' && data[pos] != '/') {
                size_t attr_name_start = pos;
                if (!parshred::is_name_start(data[pos])) break;
                ++pos;
                while (pos < len && parshred::is_name_char(data[pos])) ++pos;
                size_t attr_name_end = pos;

                while (pos < len && parshred::is_whitespace(data[pos])) ++pos;

                const char* val_ptr = nullptr;
                size_t val_len = 0;

                if (pos < len && data[pos] == '=') {
                    ++pos;
                    while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
                    if (pos < len && (data[pos] == '"' || data[pos] == '\'')) {
                        char quote = data[pos];
                        ++pos;
                        size_t val_start = pos;
                        pos = find_char_fast(data, pos, len, quote);
                        val_ptr = data + val_start;
                        val_len = pos - val_start;
                        if (pos < len) ++pos;
                    }
                }

                if constexpr (!(Flags & FDOM_NO_ATTRS)) {
                    ensure_capacity();
                    uint32_t attr_idx = node_count;
                    nodes[node_count] = FastNode{};
                    nodes[node_count].type = 6;  // Attribute type
                    nodes[node_count].name_ptr = data + attr_name_start;
                    nodes[node_count].name_len = static_cast<uint32_t>(attr_name_end - attr_name_start);
                    // Point value directly into source buffer (zero-copy)
                    // We repurpose value_offset as a raw pointer offset from data start
                    nodes[node_count].value_offset = val_ptr ? static_cast<uint32_t>(val_ptr - data) : 0;
                    nodes[node_count].value_len = static_cast<uint16_t>(std::min(val_len, size_t(65535)));
                    ++node_count;

                    if (last_attr_idx_local) {
                        nodes[last_attr_idx_local].next_sibling = attr_idx;
                    } else {
                        nodes[elem_idx].first_attr = attr_idx;
                    }
                    last_attr_idx_local = attr_idx;
                }

                while (pos < len && parshred::is_whitespace(data[pos])) ++pos;
            }

            // Self-closing?
            bool self_closing = false;
            if (pos < len && data[pos] == '/') {
                self_closing = true;
                ++pos;
            }
            if (pos < len && data[pos] == '>') {
                ++pos;
            }

            if (!self_closing) {
                if (stack_top < 255) {
                    ++stack_top;
                    stack[stack_top] = {elem_idx, 0};
                }
            }
        }
    }

    dom.node_count = node_count;
    return dom;
}

} // namespace parshred
