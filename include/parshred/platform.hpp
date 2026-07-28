#pragma once
/// @file platform.hpp
/// @brief Platform abstraction layer for parshred.
///
/// Provides compiler intrinsics, SIMD detection, aligned allocation,
/// memory-mapped file access, and CPUID queries with a portable interface
/// that works on Linux, macOS, and Windows (MSVC).

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>

// ── Platform detection ─────────────────────────────────────────────────
#if defined(_WIN32)
#define PARSHRED_WINDOWS 1
#elif defined(__linux__)
#define PARSHRED_LINUX 1
#elif defined(__APPLE__)
#define PARSHRED_MACOS 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define PARSHRED_ARM64 1
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define PARSHRED_X86_64 1
#endif

// ── Compiler abstraction ───────────────────────────────────────────────
#if defined(_MSC_VER)
#define PARSHRED_FORCE_INLINE __forceinline
#define PARSHRED_LIKELY(x) (x)
#define PARSHRED_UNLIKELY(x) (x)
#define PARSHRED_ALIGNED(n) __declspec(align(n))
#define PARSHRED_EXPORT __declspec(dllexport)

// MSVC has no __builtin_*; provide portable shims used by the SIMD scanners
// and the DOM hot paths. <intrin.h> supplies _BitScanForward / _BitScanForward64
// / __popcnt / _lzcnt_u32. We wrap them in the parshred namespace so callers
// use parshred::ctz / parshred::ctzll / parshred::popcount / parshred::clz.
#include <intrin.h>
namespace parshred {
    inline int ctz(unsigned x) {
        unsigned long r; _BitScanForward(&r, x); return static_cast<int>(r);
    }
    inline int ctzll(unsigned long long x) {
        unsigned long r; _BitScanForward64(&r, x); return static_cast<int>(r);
    }
    inline int popcount(unsigned x) { return static_cast<int>(__popcnt(x)); }
    inline int clz(unsigned x) { return static_cast<int>(__lzcnt(x)); }
}
#else
#define PARSHRED_FORCE_INLINE __attribute__((always_inline)) inline
#define PARSHRED_LIKELY(x) __builtin_expect(!!(x), 1)
#define PARSHRED_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define PARSHRED_ALIGNED(n) __attribute__((aligned(n)))
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define PARSHRED_EXPORT __attribute__((visibility("default")))
#else
#define PARSHRED_EXPORT
#endif

// GCC/Clang counterparts of the MSVC shims above. Same parshred:: namespace
// so call sites are compiler-agnostic.
namespace parshred {
    inline int ctz(unsigned x)       { return __builtin_ctz(x); }
    inline int ctzll(unsigned long long x) { return __builtin_ctzll(x); }
    inline int popcount(unsigned x) { return __builtin_popcount(x); }
    inline int clz(unsigned x)       { return __builtin_clz(x); }
}
#endif

// ── SIMD compile-time detection ──────────────────────────────────────────
#if defined(__AVX2__)
#define PARSHRED_HAS_AVX2 1
#endif

#if defined(__AVX512F__) && defined(__AVX512BW__)
#define PARSHRED_HAS_AVX512 1
#endif

#if defined(__SSE4_2__)
#define PARSHRED_HAS_SSE42 1
#endif

#if defined(__ARM_NEON)
#define PARSHRED_HAS_NEON 1
#endif

// ── OS headers ─────────────────────────────────────────────────────────
#if PARSHRED_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#include <vector>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#endif

#if PARSHRED_WINDOWS
#include <intrin.h>
#endif

namespace parshred {

namespace detail {

/// Build a system error message from the last OS error.
inline std::string make_platform_error(const std::string& prefix) {
#if PARSHRED_WINDOWS
    std::error_code ec(static_cast<int>(GetLastError()), std::system_category());
#else
    std::error_code ec(errno, std::system_category());
#endif
    return prefix + ": " + ec.message();
}

#if !PARSHRED_WINDOWS
struct FileDescriptor {
    int fd = -1;
    explicit FileDescriptor(int f) noexcept : fd(f) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    ~FileDescriptor() {
        if (fd >= 0) ::close(fd);
    }
};
#endif

} // namespace detail

// ── Aligned allocation ─────────────────────────────────────────────────

/// Allocates @p size bytes aligned to @p alignment.
/// Throws std::bad_alloc on failure.
inline void* aligned_malloc(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }
#if PARSHRED_WINDOWS
    void* ptr = _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
#if defined(__APPLE__)
    // libc++ on Apple platforms does not always expose std::aligned_alloc,
    // so fall back to the POSIX API.
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#else
    // std::aligned_alloc requires the size to be a multiple of the alignment.
    const size_t mask = alignment - 1;
    const size_t rounded = (size + mask) & ~mask;
    ptr = std::aligned_alloc(alignment, rounded);
#endif
#endif
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

/// Frees memory allocated with aligned_malloc.
inline void aligned_free(void* ptr) noexcept {
#if PARSHRED_WINDOWS
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

// ── CPU feature queries ────────────────────────────────────────────────

#if PARSHRED_X86_64 || defined(_M_IX86) || defined(__i386__)
#define PARSHRED_CPUID_AVAILABLE 1
#endif

#if PARSHRED_CPUID_AVAILABLE && PARSHRED_WINDOWS
namespace detail {

inline bool cpuid_bit(int leaf, int subleaf, int reg, int bit) {
    int info[4] = {0};
    if (subleaf == 0) {
        __cpuid(info, leaf);
    } else {
        __cpuidex(info, leaf, subleaf);
    }
    return (info[reg] & (1 << bit)) != 0;
}

inline bool osxsave_enabled() {
    return cpuid_bit(1, 0, 2, 27);
}

inline bool avx_os_enabled() {
    if (!osxsave_enabled()) return false;
    unsigned long long xcr0 = _xgetbv(0);
    return (xcr0 & 0x6ULL) == 0x6ULL;
}

inline bool avx512_os_enabled() {
    if (!avx_os_enabled()) return false;
    unsigned long long xcr0 = _xgetbv(0);
    return (xcr0 & 0xE6ULL) == 0xE6ULL;
}

} // namespace detail
#endif

/// Returns true if the CPU supports SSE4.2.
inline bool cpu_has_sse42() {
#if PARSHRED_CPUID_AVAILABLE
#if PARSHRED_WINDOWS
    return detail::cpuid_bit(1, 0, 2, 20);
#else
    return __builtin_cpu_supports("sse4.2");
#endif
#else
    return false;
#endif
}

/// Returns true if the CPU supports AVX2.
inline bool cpu_has_avx2() {
#if PARSHRED_CPUID_AVAILABLE
#if PARSHRED_WINDOWS
    return detail::avx_os_enabled() && detail::cpuid_bit(7, 0, 1, 5);
#else
    return __builtin_cpu_supports("avx2");
#endif
#else
    return false;
#endif
}

/// Returns true if the CPU supports AVX-512 Foundation and Byte/Word extensions.
inline bool cpu_has_avx512() {
#if PARSHRED_CPUID_AVAILABLE
#if PARSHRED_WINDOWS
    return detail::avx512_os_enabled() &&
           detail::cpuid_bit(7, 0, 1, 16) &&
           detail::cpuid_bit(7, 0, 1, 30);
#else
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
#endif
#else
    return false;
#endif
}

// ── Memory-mapped file ─────────────────────────────────────────────────

/// A portable read-only memory-mapped file.
///
/// The class is move-only. The destructor automatically unmaps the file.
class MappedFile {
public:
    MappedFile() noexcept = default;
    ~MappedFile() { close(); }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

#if PARSHRED_WINDOWS
    MappedFile(MappedFile&& other) noexcept
        : data_(other.data_),
          size_(other.size_),
          file_(other.file_),
          mapping_(other.mapping_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.file_ = INVALID_HANDLE_VALUE;
        other.mapping_ = nullptr;
    }

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            close();
            data_ = other.data_;
            size_ = other.size_;
            file_ = other.file_;
            mapping_ = other.mapping_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.file_ = INVALID_HANDLE_VALUE;
            other.mapping_ = nullptr;
        }
        return *this;
    }
#else
    MappedFile(MappedFile&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            close();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
#endif

    /// Open and map the file at @p path. Throws std::runtime_error on failure.
    static MappedFile open(const char* path) {
        if (!path) {
            throw std::runtime_error("MappedFile::open: null path");
        }
#if PARSHRED_WINDOWS
        // Convert UTF-8 path to UTF-16 for the Windows API.
        int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        if (len == 0) {
            throw std::runtime_error(detail::make_platform_error(path));
        }
        std::vector<wchar_t> wpath(static_cast<size_t>(len));
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), len) == 0) {
            throw std::runtime_error(detail::make_platform_error(path));
        }

        MappedFile mf;
        mf.file_ = CreateFileW(wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mf.file_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error(detail::make_platform_error(path));
        }

        LARGE_INTEGER file_size;
        if (!GetFileSizeEx(mf.file_, &file_size)) {
            mf.close();
            throw std::runtime_error(detail::make_platform_error(path));
        }
        mf.size_ = static_cast<size_t>(file_size.QuadPart);

        if (mf.size_ > 0) {
            mf.mapping_ = CreateFileMappingW(mf.file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!mf.mapping_) {
                mf.close();
                throw std::runtime_error(detail::make_platform_error(path));
            }
            void* view = MapViewOfFile(mf.mapping_, FILE_MAP_READ, 0, 0, 0);
            if (!view) {
                mf.close();
                throw std::runtime_error(detail::make_platform_error(path));
            }
            mf.data_ = static_cast<const char*>(view);
        }
        return mf;
#else
        detail::FileDescriptor fd(::open(path, O_RDONLY));
        if (fd.fd < 0) {
            throw std::runtime_error(detail::make_platform_error(path));
        }

        struct stat st;
        if (::fstat(fd.fd, &st) < 0) {
            throw std::runtime_error(detail::make_platform_error(path));
        }

        MappedFile mf;
        mf.size_ = static_cast<size_t>(st.st_size);
        if (mf.size_ > 0) {
            void* addr = ::mmap(nullptr, mf.size_, PROT_READ, MAP_PRIVATE, fd.fd, 0);
            if (addr == MAP_FAILED) {
                throw std::runtime_error(detail::make_platform_error(path));
            }
            mf.data_ = static_cast<const char*>(addr);
        }
        return mf;
#endif
    }

    /// Returns a pointer to the mapped data, or nullptr for an empty file.
    [[nodiscard]] const char* data() const noexcept { return data_; }

    /// Returns the size of the mapped file in bytes.
    [[nodiscard]] size_t size() const noexcept { return size_; }

    /// Unmap the file and release OS resources.
    void close() noexcept {
#if PARSHRED_WINDOWS
        if (data_) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_ && size_ > 0) {
            ::munmap(const_cast<char*>(data_), size_);
        }
        data_ = nullptr;
#endif
        size_ = 0;
    }

private:
    const char* data_ = nullptr;
    size_t size_ = 0;
#if PARSHRED_WINDOWS
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#endif
};

} // namespace parshred
