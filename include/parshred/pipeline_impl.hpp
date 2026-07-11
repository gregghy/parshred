#pragma once
/// @file pipeline_impl.hpp
/// @brief Template implementation for chunked and parallel parsers.

#include <parshred/pipeline.hpp>
#include <parshred/simd_utils.hpp>

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace parshred {

// ── ChunkedParser ────────────────────────────────────────────────────

template<ParseMode Mode>
size_t ChunkedParser<Mode>::find_safe_boundary(
    const char* data, size_t start, size_t end) noexcept
{
    // Search backwards from `end` to find the last '<' that is not
    // inside a quoted attribute value. We look for '<' because it
    // always starts a new tag/comment/PI/CDATA.
    //
    // Simplified approach: just find the last '<' in the overlap region.
    // If a tag is longer than overlap (64 KB), that's pathological XML.
    size_t search_start = (end > start + 256) ? end - 256 : start;

    for (size_t i = end; i > search_start; --i) {
        if (data[i - 1] == '<') {
            return i - 1;
        }
    }
    // No '<' found in the last 256 bytes — extremely unlikely.
    // Fall back to the original end position.
    return end;
}

template<ParseMode Mode>
template<typename Handler>
void ChunkedParser<Mode>::parse(const char* data, size_t len, Handler& handler) {
    bytes_processed_ = 0;
    boundary_buf_.clear();

    // Use a single parser instance across all chunks to preserve state
    // (depth tracking, entity count, etc.) across chunk boundaries.
    FastSaxParser<Mode> parser;
    size_t pos = 0;
    bool first_chunk = true;

    while (pos < len) {
        size_t chunk_end = std::min(pos + config_.chunk_size, len);
        size_t safe_end = chunk_end;

        if (chunk_end < len) {
            // Find a safe boundary to avoid splitting mid-tag
            safe_end = find_safe_boundary(data, pos, chunk_end);
        }

        // If we have leftover from the previous chunk boundary,
        // prepend it to the current chunk.
        if (!boundary_buf_.empty()) {
            boundary_buf_.append(data + pos, safe_end - pos);
            if (first_chunk) {
                parser.parse(boundary_buf_.data(), boundary_buf_.size(), handler);
                first_chunk = false;
            } else {
                parser.parse_chunk(boundary_buf_.data(), boundary_buf_.size(), handler);
            }
            boundary_buf_.clear();
        } else {
            if (first_chunk) {
                parser.parse(data + pos, safe_end - pos, handler);
                first_chunk = false;
            } else {
                parser.parse_chunk(data + pos, safe_end - pos, handler);
            }
        }

        bytes_processed_ += (safe_end - pos);

        // If we split before chunk_end, save the partial data
        if (safe_end < chunk_end) {
            boundary_buf_.assign(data + safe_end, chunk_end - safe_end);
        }

        pos = chunk_end;
    }

    // Handle any remaining boundary data
    if (!boundary_buf_.empty()) {
        if (first_chunk) {
            parser.parse(boundary_buf_.data(), boundary_buf_.size(), handler);
        } else {
            parser.parse_chunk(boundary_buf_.data(), boundary_buf_.size(), handler);
        }
        boundary_buf_.clear();
    }
}

template<ParseMode Mode>
template<typename Handler>
void ChunkedParser<Mode>::parse_file(const std::string& path, Handler& handler) {
    MmapReader reader;
    reader.open(path);
    auto d = reader.data();
    parse(d.data(), d.size(), handler);
}

// ── ParallelParser ───────────────────────────────────────────────────

template<ParseMode Mode>
template<typename Handler>
void ParallelParser<Mode>::parse(const char* data, size_t len, Handler& handler) {
    bytes_processed_ = 0;

    // For the parallel parser, we use a pipeline:
    //   Thread pool prefetches upcoming chunks while the main thread parses.
    //
    // Since SAX callbacks are serial, the main parse loop stays on the
    // calling thread. Parallelism comes from overlapping I/O with compute.

    FastSaxParser<Mode> parser;
    std::string boundary_buf;
    size_t pos = 0;
    bool first_chunk = true;
    const size_t chunk_size = config_.chunk_size;

    // Prefetch the first chunk
#ifdef __linux__
    if (config_.prefetch && len > 0) {
        size_t prefetch_end = std::min(chunk_size * 2, len);
        ::madvise(const_cast<char*>(data), prefetch_end, MADV_SEQUENTIAL);
        ::madvise(const_cast<char*>(data), prefetch_end, MADV_WILLNEED);
    }
#endif

    auto do_parse = [&](const char* d, size_t l) {
        if (first_chunk) {
            parser.parse(d, l, handler);
            first_chunk = false;
        } else {
            parser.parse_chunk(d, l, handler);
        }
    };

    while (pos < len) {
        size_t chunk_end = std::min(pos + chunk_size, len);
        size_t safe_end = chunk_end;

        if (chunk_end < len) {
            safe_end = ChunkedParser<Mode>::find_safe_boundary(data, pos, chunk_end);
        }

        // Prefetch the NEXT chunk while we parse the current one
#ifdef __linux__
        if (config_.prefetch && chunk_end < len) {
            size_t next_prefetch = std::min(chunk_end + chunk_size * 2, len);
            ::madvise(const_cast<char*>(data + chunk_end),
                      next_prefetch - chunk_end, MADV_WILLNEED);
        }
#endif

        // Parse current chunk
        if (!boundary_buf.empty()) {
            boundary_buf.append(data + pos, safe_end - pos);
            do_parse(boundary_buf.data(), boundary_buf.size());
            boundary_buf.clear();
        } else {
            do_parse(data + pos, safe_end - pos);
        }

        bytes_processed_ += (safe_end - pos);

        if (safe_end < chunk_end) {
            boundary_buf.assign(data + safe_end, chunk_end - safe_end);
        }

        pos = chunk_end;
    }

    if (!boundary_buf.empty()) {
        do_parse(boundary_buf.data(), boundary_buf.size());
        boundary_buf.clear();
    }
}

template<ParseMode Mode>
template<typename Handler>
void ParallelParser<Mode>::parse_file(const std::string& path, Handler& handler) {
    MmapReader reader;
    reader.open(path);
    auto d = reader.data();

#ifdef __linux__
    // Advise the kernel about our access pattern for the entire file
    if (config_.prefetch) {
        ::madvise(const_cast<char*>(d.data()), d.size(), MADV_SEQUENTIAL);
    }
#endif

    parse(d.data(), d.size(), handler);
}

} // namespace parshred
