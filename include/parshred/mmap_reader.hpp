#pragma once
/// @file mmap_reader.hpp
/// @brief Zero-copy file reading via memory-mapped I/O.

#include <parshred/common.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace parshred {

/// Reads a file into memory via mmap(2) for zero-copy access.
///
/// For small files or stdin, falls back to a heap-allocated buffer.
/// The returned span is valid for the lifetime of this object.
class MmapReader {
public:
    /// Minimum file size to use mmap (below this, just read into a buffer).
    static constexpr size_t MMAP_THRESHOLD = 4096;

    MmapReader() = default;
    ~MmapReader();

    // Non-copyable, movable
    MmapReader(const MmapReader&) = delete;
    MmapReader& operator=(const MmapReader&) = delete;
    MmapReader(MmapReader&&) noexcept;
    MmapReader& operator=(MmapReader&&) noexcept;

    /// Open and map a file. Throws IOError on failure.
    void open(const std::string& path);

    /// Load from an existing buffer (copies the data).
    void load_buffer(const char* data, size_t size);

    /// Load from a string (copies the data).
    void load_string(std::string_view str);

    /// Read all of stdin into a buffer.
    void load_stdin();

    /// Returns a span over the mapped/loaded data.
    [[nodiscard]] std::span<const char> data() const noexcept;

    /// Returns the size in bytes.
    [[nodiscard]] size_t size() const noexcept;

    /// Whether a file is currently loaded.
    [[nodiscard]] bool is_open() const noexcept;

    /// Close and unmap.
    void close() noexcept;

private:
    const char* data_  = nullptr;
    size_t      size_  = 0;
    bool        is_mmap_ = false;
    bool        is_open_ = false;
    std::unique_ptr<char[]> buffer_; // for non-mmap fallback
};

} // namespace parshred
