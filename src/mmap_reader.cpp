/// @file mmap_reader.cpp
/// @brief Memory-mapped file I/O implementation.

#include <parshred/mmap_reader.hpp>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
// ── Windows implementation: CreateFileMapping / MapViewOfFile ─────────
#include <windows.h>
#include <fileapi.h>
#else
// ── POSIX implementation: open / mmap / madvise ───────────────────────
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace parshred {

MmapReader::~MmapReader() {
    close();
}

MmapReader::MmapReader(MmapReader&& other) noexcept
    : data_(other.data_), size_(other.size_), is_mmap_(other.is_mmap_),
      is_open_(other.is_open_), buffer_(std::move(other.buffer_))
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.is_mmap_ = false;
    other.is_open_ = false;
}

MmapReader& MmapReader::operator=(MmapReader&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        is_mmap_ = other.is_mmap_;
        is_open_ = other.is_open_;
        buffer_ = std::move(other.buffer_);
        other.data_ = nullptr;
        other.size_ = 0;
        other.is_mmap_ = false;
        other.is_open_ = false;
    }
    return *this;
}

void MmapReader::open(const std::string& path) {
    close();

#if defined(_WIN32)
    // ── Windows path ──────────────────────────────────────────────────
    // Open the file with read access, then create a file mapping and map
    // a read-only view. For files under MMAP_THRESHOLD we fall back to a
    // plain heap buffer (same as POSIX) to avoid mapping overhead.
    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw IOError("Failed to open file '" + path + "': Windows error " +
                      std::to_string(::GetLastError()));
    }

    LARGE_INTEGER fsz;
    if (!::GetFileSizeEx(h, &fsz)) {
        ::CloseHandle(h);
        throw IOError("Failed to stat file '" + path + "': Windows error " +
                      std::to_string(::GetLastError()));
    }
    size_ = static_cast<size_t>(fsz.QuadPart);

    if (size_ == 0) {
        ::CloseHandle(h);
        data_ = nullptr;
        is_mmap_ = false;
        is_open_ = true;
        return;
    }

    if (size_ < MMAP_THRESHOLD) {
        buffer_ = std::make_unique<char[]>(size_);
        DWORD nread = 0;
        if (!::ReadFile(h, buffer_.get(), static_cast<DWORD>(size_), &nread, nullptr) ||
            nread != size_) {
            ::CloseHandle(h);
            throw IOError("Failed to read file '" + path + "': Windows error " +
                          std::to_string(::GetLastError()));
        }
        size_ = nread;
        data_ = buffer_.get();
        is_mmap_ = false;
        is_open_ = true;
        ::CloseHandle(h);
        return;
    }

    HANDLE mapping = ::CreateFileMappingA(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    ::CloseHandle(h);
    if (!mapping) {
        throw IOError("Failed to mmap file '" + path + "': Windows error " +
                      std::to_string(::GetLastError()));
    }
    void* mapped = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    ::CloseHandle(mapping);
    if (!mapped) {
        throw IOError("Failed to mmap file '" + path + "': Windows error " +
                      std::to_string(::GetLastError()));
    }
    data_ = static_cast<const char*>(mapped);
    is_mmap_ = true;
    is_open_ = true;
#else
    // ── POSIX path ─────────────────────────────────────────────────────
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw IOError("Failed to open file '" + path + "': " + std::strerror(errno));
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        throw IOError("Failed to stat file '" + path + "': " + std::strerror(errno));
    }

    size_ = static_cast<size_t>(st.st_size);

    if (size_ == 0) {
        // Empty file — valid but nothing to map
        ::close(fd);
        data_ = nullptr;
        is_mmap_ = false;
        is_open_ = true;
        return;
    }

    if (size_ < MMAP_THRESHOLD) {
        // Small file — just read it
        buffer_ = std::make_unique<char[]>(size_);
        ssize_t nread = 0;
        size_t total = 0;
        while (total < size_) {
            nread = ::read(fd, buffer_.get() + total, size_ - total);
            if (nread < 0) {
                ::close(fd);
                throw IOError("Failed to read file '" + path + "': " + std::strerror(errno));
            }
            if (nread == 0) break;
            total += static_cast<size_t>(nread);
        }
        size_ = total;
        data_ = buffer_.get();
        is_mmap_ = false;
        is_open_ = true;
        ::close(fd);
        return;
    }

    // mmap the file. Use MAP_POPULATE to pre-fault pages — this avoids
    // thousands of page faults during parsing, which is critical for
    // large files (>1 MB). The upfront cost is amortized over the scan.
    // MAP_HUGETLB is not used because it requires huge page reservation
    // and can fail silently; we use madvise(MADV_HUGEPAGE) instead to
    // let the kernel coalesce pages into transparent huge pages (THP).
    int flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    flags |= MAP_POPULATE;
#endif
    void* mapped = ::mmap(nullptr, size_, PROT_READ, flags, fd, 0);
    ::close(fd);

    if (mapped == MAP_FAILED) {
        throw IOError("Failed to mmap file '" + path + "': " + std::strerror(errno));
    }

    // Advise the kernel that we'll read sequentially (read-ahead prefetch)
    ::madvise(mapped, size_, MADV_SEQUENTIAL);
    // Request transparent huge pages to reduce TLB misses on large files.
    // The kernel may ignore this if THP is disabled system-wide, but it's
    // a no-op cost hint that helps when available.
#ifdef MADV_HUGEPAGE
    ::madvise(mapped, size_, MADV_HUGEPAGE);
#endif

    data_ = static_cast<const char*>(mapped);
    is_mmap_ = true;
    is_open_ = true;
#endif
}

void MmapReader::load_buffer(const char* data, size_t size) {
    close();
    if (size == 0) {
        data_ = nullptr;
        size_ = 0;
        is_open_ = true;
        return;
    }
    buffer_ = std::make_unique<char[]>(size);
    std::memcpy(buffer_.get(), data, size);
    data_ = buffer_.get();
    size_ = size;
    is_mmap_ = false;
    is_open_ = true;
}

void MmapReader::load_string(std::string_view str) {
    load_buffer(str.data(), str.size());
}

void MmapReader::load_stdin() {
    close();
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string content = oss.str();
    load_buffer(content.data(), content.size());
}

std::span<const char> MmapReader::data() const noexcept {
    return {data_, size_};
}

size_t MmapReader::size() const noexcept {
    return size_;
}

bool MmapReader::is_open() const noexcept {
    return is_open_;
}

void MmapReader::close() noexcept {
    if (is_mmap_ && data_ != nullptr) {
#if defined(_WIN32)
        ::UnmapViewOfFile(const_cast<char*>(data_));
#else
        ::munmap(const_cast<char*>(data_), size_);
#endif
    }
    data_ = nullptr;
    size_ = 0;
    is_mmap_ = false;
    is_open_ = false;
    buffer_.reset();
}

} // namespace parshred
