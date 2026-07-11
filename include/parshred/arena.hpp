#pragma once
/// @file arena.hpp
/// @brief Fast bump allocator for temporary parsing allocations.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace parshred {

/// A simple arena (bump) allocator.
///
/// Allocates memory from large blocks, with O(1) allocation cost.
/// Memory is freed all at once when the arena is destroyed or reset.
/// Not thread-safe — use one arena per thread.
class Arena {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024; // 64 KB

    explicit Arena(size_t block_size = DEFAULT_BLOCK_SIZE);
    ~Arena();

    // Non-copyable, movable
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) noexcept;
    Arena& operator=(Arena&&) noexcept;

    /// Allocate `size` bytes with `alignment` alignment.
    /// Returns nullptr only if the system is out of memory.
    [[nodiscard]] void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    /// Allocate and construct an object of type T.
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    /// Allocate an array of `count` elements of type T (uninitialized).
    template <typename T>
    [[nodiscard]] T* allocate_array(size_t count) {
        return static_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    /// Free all memory (but keep block storage for reuse).
    void reset() noexcept;

    /// Total bytes allocated across all blocks.
    [[nodiscard]] size_t total_allocated() const noexcept;

    /// Number of blocks currently held.
    [[nodiscard]] size_t block_count() const noexcept;

private:
    struct Block {
        std::unique_ptr<uint8_t[]> data;
        size_t                     capacity;
    };

    size_t              block_size_;
    std::vector<Block>  blocks_;
    size_t              current_block_ = 0;
    size_t              offset_        = 0; // offset within current block

    void grow(size_t min_size);
};

} // namespace parshred
