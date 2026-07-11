/// @file mmap_reader.cpp
/// @brief Memory-mapped file I/O implementation.

#include <parshred/mmap_reader.hpp>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

    // mmap the file
    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (mapped == MAP_FAILED) {
        throw IOError("Failed to mmap file '" + path + "': " + std::strerror(errno));
    }

    // Advise the kernel that we'll read sequentially
    ::madvise(mapped, size_, MADV_SEQUENTIAL);

    data_ = static_cast<const char*>(mapped);
    is_mmap_ = true;
    is_open_ = true;
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
        ::munmap(const_cast<char*>(data_), size_);
    }
    data_ = nullptr;
    size_ = 0;
    is_mmap_ = false;
    is_open_ = false;
    buffer_.reset();
}

} // namespace parshred
