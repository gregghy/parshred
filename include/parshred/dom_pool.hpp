#pragma once
/// @file dom_pool.hpp
/// @brief Ultra-fast typed pool allocator for DOM nodes.
///
/// Designed for maximum allocation speed:
///   - Allocating a node = increment a pointer (single instruction)
///   - No per-node deallocation (bulk free on pool destruction)
///   - Pages are 4 KB aligned for TLB efficiency
///   - Pre-estimates capacity from file size to avoid reallocation
///
/// For a 16 KB XML file with ~170 elements, each with 2–3 attributes,
/// we might need ~700 XmlNode allocations. At 80 bytes/node, that's
/// ~56 KB of pool memory — fits in L1 or L2 cache.

#include <parshred/dom.hpp>
#include <parshred/platform.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace parshred {

/// Fixed-type pool allocator for XmlNode.
///
/// Allocates XmlNode objects from contiguous pages. Each page holds
/// a fixed number of nodes. When a page is full, a new one is allocated.
///
/// This is significantly faster than malloc/new because:
///   1. No free-list management
///   2. No size/alignment computation per allocation
///   3. Perfect spatial locality (nodes allocated consecutively)
///   4. Single bulk-free on destruction
class NodePool {
public:
    /// Nodes per page. 512 nodes × 80 bytes = 40 KB per page (fits L1).
    static constexpr size_t NODES_PER_PAGE = 512;
    static constexpr size_t PAGE_SIZE = NODES_PER_PAGE * sizeof(XmlNode);

    NodePool() = default;

    /// Pre-allocate capacity for an estimated number of nodes.
    /// Call this before parsing to avoid mid-parse page allocations.
    explicit NodePool(size_t estimated_nodes) {
        reserve(estimated_nodes);
    }

    ~NodePool() {
        for (auto* page : pages_) {
            parshred::aligned_free(page);
        }
    }

    // Non-copyable, movable
    NodePool(const NodePool&) = delete;
    NodePool& operator=(const NodePool&) = delete;

    NodePool(NodePool&& o) noexcept
        : pages_(std::move(o.pages_)),
          current_page_(o.current_page_),
          current_page_idx_(o.current_page_idx_),
          offset_(o.offset_),
          total_nodes_(o.total_nodes_) {
        o.current_page_ = nullptr;
        o.current_page_idx_ = 0;
        o.offset_ = NODES_PER_PAGE;
        o.total_nodes_ = 0;
    }

    NodePool& operator=(NodePool&& o) noexcept {
        if (this != &o) {
            for (auto* p : pages_) parshred::aligned_free(p);
            pages_ = std::move(o.pages_);
            current_page_ = o.current_page_;
            current_page_idx_ = o.current_page_idx_;
            offset_ = o.offset_;
            total_nodes_ = o.total_nodes_;
            o.current_page_ = nullptr;
            o.current_page_idx_ = 0;
            o.offset_ = NODES_PER_PAGE;
            o.total_nodes_ = 0;
        }
        return *this;
    }

    /// Allocate a single node. Zero-initialized.
    /// Cost: increment pointer + conditional page allocation (rare).
    [[nodiscard]] PARSHRED_FORCE_INLINE XmlNode* allocate() {
        if (PARSHRED_UNLIKELY(offset_ >= NODES_PER_PAGE)) {
            grow();
        }
        XmlNode* node = current_page_ + offset_;
        ++offset_;
        ++total_nodes_;
        return node;
    }

    /// Allocate a node and initialize its type.
    [[nodiscard]] PARSHRED_FORCE_INLINE XmlNode* allocate(NodeType type) {
        XmlNode* node = allocate();
        node->type = type;
        return node;
    }

    /// Pre-allocate pages for `n` nodes.
    void reserve(size_t n) {
        size_t pages_needed = (n + NODES_PER_PAGE - 1) / NODES_PER_PAGE;
        pages_.reserve(pages_needed);
    }

    /// Total nodes allocated.
    [[nodiscard]] size_t size() const noexcept { return total_nodes_; }

    /// Total bytes allocated in pages.
    [[nodiscard]] size_t bytes_allocated() const noexcept {
        return pages_.size() * PAGE_SIZE;
    }

    /// Reset pool for reuse (keeps page allocations).
    void reset() noexcept {
        if (!pages_.empty()) {
            // Re-initialize all nodes to default state
            for (auto* page : pages_) {
                for (size_t i = 0; i < NODES_PER_PAGE; ++i) {
                    page[i] = XmlNode{};
                }
            }
            current_page_ = pages_[0];
            current_page_idx_ = 0;
            offset_ = 0;
            total_nodes_ = 0;
        }
    }

private:
    std::vector<XmlNode*> pages_;
    XmlNode* current_page_ = nullptr;
    size_t current_page_idx_ = 0;  // O(1) grow: index of current page
    size_t offset_ = NODES_PER_PAGE;  // Force initial grow
    size_t total_nodes_ = 0;

    void grow() {
        // O(1) page advance: check if the next pre-allocated page exists.
        if (current_page_idx_ + 1 < pages_.size()) {
            ++current_page_idx_;
            current_page_ = pages_[current_page_idx_];
            offset_ = 0;
            return;
        }

        // Allocate new page (aligned for cache efficiency).
        // Use parshred::aligned_malloc for portability (MSVC has no
        // std::aligned_alloc; macOS libc++ doesn't always expose it).
        void* raw = parshred::aligned_malloc(PAGE_SIZE, 64);
        if (!raw) throw std::bad_alloc();
        // Zero the page — cheaper than placement-new for trivial types
        // XmlNode is trivially-destructible (only raw pointers + string_view)
        std::memset(raw, 0, PAGE_SIZE);
        current_page_ = static_cast<XmlNode*>(raw);
        pages_.push_back(current_page_);
        current_page_idx_ = pages_.size() - 1;
        offset_ = 0;
    }
};

/// Estimate the number of nodes needed for a file of given size.
/// Heuristic: ~1 node per 50–100 bytes of XML (elements + attrs + text).
/// We use 60 bytes as a conservative estimate.
inline size_t estimate_node_count(size_t file_size) noexcept {
    return file_size / 60 + 16;
}

/// String pool for arena-mode: stores expanded entity values.
/// Small strings that need to outlive the parse but aren't in the
/// source buffer (e.g., expanded "&amp;" → "&").
class StringPool {
public:
    StringPool() = default;

    /// Store a string and return a view to the pool-owned copy.
    [[nodiscard]] std::string_view store(std::string_view s) {
        if (s.empty()) return {};
        // Allocate from current page
        if (offset_ + s.size() > PAGE_SIZE) {
            grow(s.size());
        }
        char* dest = current_page_ + offset_;
        std::memcpy(dest, s.data(), s.size());
        offset_ += s.size();
        return {dest, s.size()};
    }

    ~StringPool() {
        for (auto* page : pages_) {
            std::free(page);
        }
    }

    StringPool(const StringPool&) = delete;
    StringPool& operator=(const StringPool&) = delete;
    StringPool(StringPool&&) noexcept = default;
    StringPool& operator=(StringPool&&) noexcept = default;

private:
    static constexpr size_t PAGE_SIZE = 64 * 1024;  // 64 KB pages
    std::vector<char*> pages_;
    char* current_page_ = nullptr;
    size_t offset_ = PAGE_SIZE;  // Force initial grow

    void grow(size_t min_size) {
        size_t alloc_size = std::max(PAGE_SIZE, min_size + 64);
        char* page = static_cast<char*>(std::malloc(alloc_size));
        if (!page) throw std::bad_alloc();
        pages_.push_back(page);
        current_page_ = page;
        offset_ = 0;
    }
};

} // namespace parshred
