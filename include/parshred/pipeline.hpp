#pragma once
/// @file pipeline.hpp
/// @brief Chunked streaming and parallel pipeline for large XML files.
///
/// For files up to 10 GB (or larger), we cannot build a full structural
/// index in memory. Instead, we process the file in fixed-size chunks:
///
///   chunk N:   [madvise prefetch] → [SIMD-accelerated parse] → [callbacks]
///   chunk N+1: [madvise prefetch] → [parse] → ...
///
/// The pipeline can operate in two modes:
///   1. Single-threaded chunked: constant-memory streaming
///   2. Pipelined parallel: overlap I/O prefetch with parsing
///
/// Key challenge: XML tags can span chunk boundaries. We handle this
/// with a small "overlap" region: when a chunk ends mid-tag, we save
/// the partial tag and prepend it to the next chunk.

#include <parshred/fast_sax.hpp>
#include <parshred/mmap_reader.hpp>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace parshred {

/// Configuration for the chunked pipeline.
struct PipelineConfig {
    size_t chunk_size = 2 * 1024 * 1024;  // 2 MB default
    size_t overlap    = 64 * 1024;         // 64 KB overlap for boundary tags
    bool   prefetch   = true;              // Use madvise for I/O prefetch
    size_t num_threads = 0;                // 0 = auto-detect
};

/// Chunked streaming SAX parser for large files.
///
/// Processes files in fixed-size chunks using the FastSaxParser,
/// handling XML elements that span chunk boundaries.
///
/// Usage:
/// @code
///   parshred::ChunkedParser parser;
///   MyHandler handler;
///   parser.parse_file("huge.xml", handler);
/// @endcode
template<ParseMode Mode = ParseMode::Turbo>
class ChunkedParser {
public:
    explicit ChunkedParser(PipelineConfig config = {}) : config_(config) {}

    /// Parse from a memory region (chunked, constant memory overhead).
    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    /// Parse from a memory-mapped file.
    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler);

    /// Get total bytes processed.
    size_t bytes_processed() const noexcept { return bytes_processed_; }

private:
    PipelineConfig config_;
    size_t bytes_processed_ = 0;

    // State carried across chunk boundaries
    std::string boundary_buf_;  // partial tag from previous chunk

    /// Find a safe split point: search backwards from `end` for a '<'
    /// that starts a tag outside of quotes. This ensures we never split
    /// in the middle of a tag.
    static size_t find_safe_boundary(const char* data, size_t start, size_t end) noexcept;

    // Allow ParallelParser to use find_safe_boundary
    template<ParseMode M> friend class ParallelParser;
};

/// Parallel pipelined parser.
///
/// Uses multiple threads to overlap:
///   - I/O prefetch (madvise) for chunk N+2
///   - Parsing of chunk N
///
/// Since SAX events must be emitted in document order, only the
/// I/O prefetch and some scanning work is parallelized. The actual
/// event dispatch remains serial.
template<ParseMode Mode = ParseMode::Turbo>
class ParallelParser {
public:
    explicit ParallelParser(PipelineConfig config = {}) : config_(config) {
        if (config_.num_threads == 0) {
            config_.num_threads = std::thread::hardware_concurrency();
            if (config_.num_threads == 0) config_.num_threads = 2;
        }
    }

    /// Parse from a memory-mapped file with parallel I/O prefetch.
    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler);

    /// Parse from a memory region.
    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    size_t bytes_processed() const noexcept { return bytes_processed_; }

private:
    PipelineConfig config_;
    size_t bytes_processed_ = 0;
};

} // namespace parshred

// Include template implementation
#include <parshred/pipeline_impl.hpp>
