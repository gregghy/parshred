/// @file arena.cpp
/// @brief Arena (bump) allocator implementation.

#include <parshred/arena.hpp>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>

namespace parshred {

Arena::Arena(size_t block_size)
    : block_size_(std::max(block_size, size_t{256}))
{
    // Pre-allocate one block
    grow(block_size_);
}

Arena::~Arena() = default;

Arena::Arena(Arena&&) noexcept = default;
Arena& Arena::operator=(Arena&&) noexcept = default;

void* Arena::allocate(size_t size, size_t alignment) {
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0); // power of 2

    if (size == 0) return nullptr;

    // Align the current offset
    size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);

    // Check if it fits in the current block
    if (current_block_ < blocks_.size()) {
        if (aligned_offset + size <= blocks_[current_block_].capacity) {
            void* ptr = blocks_[current_block_].data.get() + aligned_offset;
            offset_ = aligned_offset + size;
            return ptr;
        }
    }

    // Try the next block (if we have one from a previous reset)
    if (current_block_ + 1 < blocks_.size()) {
        ++current_block_;
        offset_ = 0;
        size_t ao = (offset_ + alignment - 1) & ~(alignment - 1);
        if (ao + size <= blocks_[current_block_].capacity) {
            void* ptr = blocks_[current_block_].data.get() + ao;
            offset_ = ao + size;
            return ptr;
        }
    }

    // Need a new block
    grow(std::max(size + alignment, block_size_));
    size_t ao = (offset_ + alignment - 1) & ~(alignment - 1);
    void* ptr = blocks_[current_block_].data.get() + ao;
    offset_ = ao + size;
    return ptr;
}

void Arena::reset() noexcept {
    current_block_ = 0;
    offset_ = 0;
}

size_t Arena::total_allocated() const noexcept {
    size_t total = 0;
    for (const auto& b : blocks_) {
        total += b.capacity;
    }
    return total;
}

size_t Arena::block_count() const noexcept {
    return blocks_.size();
}

void Arena::grow(size_t min_size) {
    size_t cap = std::max(min_size, block_size_);
    blocks_.push_back(Block{std::make_unique<uint8_t[]>(cap), cap});
    current_block_ = blocks_.size() - 1;
    offset_ = 0;
}

} // namespace parshred
