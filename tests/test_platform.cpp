#include <parshred/platform.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// ── Platform macro sanity checks ────────────────────────────────────────

TEST(PlatformMacros, ExactlyOneOsDefined) {
#if PARSHRED_WINDOWS
    const int is_windows = 1;
#else
    const int is_windows = 0;
#endif
#if PARSHRED_LINUX
    const int is_linux = 1;
#else
    const int is_linux = 0;
#endif
#if PARSHRED_MACOS
    const int is_macos = 1;
#else
    const int is_macos = 0;
#endif

    EXPECT_EQ(is_windows + is_linux + is_macos, 1);
}

TEST(PlatformMacros, ExactlyOneArchDefined) {
#if PARSHRED_X86_64
    const int x86_64 = 1;
#else
    const int x86_64 = 0;
#endif
#if PARSHRED_ARM64
    const int arm64 = 1;
#else
    const int arm64 = 0;
#endif

    EXPECT_EQ(x86_64 + arm64, 1);
}

// ── Aligned allocation ────────────────────────────────────────────────

TEST(PlatformAlignedAlloc, AllocatesAndFreesAlignedMemory) {
    constexpr size_t alignment = 64;
    constexpr size_t size = 256;

    void* ptr = parshred::aligned_malloc(size, alignment);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % alignment, 0u);

    std::memset(ptr, 0xAB, size);
    parshred::aligned_free(ptr);
}

TEST(PlatformAlignedAlloc, ZeroSizeReturnsNull) {
    void* ptr = parshred::aligned_malloc(0, 64);
    EXPECT_EQ(ptr, nullptr);
    parshred::aligned_free(ptr);
}

// ── Memory-mapped files ────────────────────────────────────────────────

TEST(PlatformMappedFile, OpensAndReadsTempFile) {
    const std::string content = "Hello from parshred platform abstraction!";
    const auto tmp_path = std::filesystem::temp_directory_path() /
                          "parshred_platform_test_mapped_file.txt";

    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        ASSERT_TRUE(ofs.is_open());
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    auto mf = parshred::MappedFile::open(tmp_path.string().c_str());
    ASSERT_EQ(mf.size(), content.size());
    ASSERT_NE(mf.data(), nullptr);
    EXPECT_EQ(std::string_view(mf.data(), mf.size()), content);

    mf.close();
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_EQ(mf.size(), 0u);

    std::filesystem::remove(tmp_path);
}

TEST(PlatformMappedFile, OpenMissingFileThrows) {
    constexpr const char* missing_path = "/parshred/this/does/not/exist.txt";
    EXPECT_THROW(parshred::MappedFile::open(missing_path), std::runtime_error);
}

// ── CPUID / SIMD runtime detection ─────────────────────────────────────

TEST(PlatformCpuId, ReportsConsistentFeatures) {
#if PARSHRED_X86_64
    const bool sse42 = parshred::cpu_has_sse42();
    const bool avx2 = parshred::cpu_has_avx2();
    const bool avx512 = parshred::cpu_has_avx512();

    // Every modern x86-64 test host should report at least SSE4.2 or AVX2.
    EXPECT_TRUE(sse42 || avx2)
        << "Expected at least one of SSE4.2/AVX2 to be present on x86_64";

    // AVX-512 implies AVX2 from an architectural standpoint.
    if (avx512) {
        EXPECT_TRUE(avx2);
    }
#elif PARSHRED_ARM64
    // Feature queries are x86-specific and should return false on ARM64.
    EXPECT_FALSE(parshred::cpu_has_sse42());
    EXPECT_FALSE(parshred::cpu_has_avx2());
    EXPECT_FALSE(parshred::cpu_has_avx512());
#else
    GTEST_SKIP() << "Unknown architecture; skipping CPU feature tests";
#endif
}
