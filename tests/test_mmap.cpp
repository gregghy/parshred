/// @file test_mmap.cpp
/// @brief Unit tests for MmapReader.

#include <parshred/mmap_reader.hpp>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace parshred;

namespace {

// Helper: create a temporary file with content.
// Uses mkstemp on POSIX, tmpnam + CreateFile on Windows.
class TempFile {
public:
    explicit TempFile(const std::string& content) {
#if defined(_WIN32)
        char tmpl[L_tmpnam];
        ::tmpnam(tmpl);
        path_ = tmpl;
        HANDLE h = ::CreateFileA(path_.c_str(), GENERIC_WRITE, 0,
                                 nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ::WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
            ::CloseHandle(h);
        }
#else
        char tmpl[] = "/tmp/parshred_test_XXXXXX";
        int fd = ::mkstemp(tmpl);
        path_ = tmpl;
        if (fd >= 0) {
            ::write(fd, content.data(), content.size());
            ::close(fd);
        }
#endif
    }
    ~TempFile() { std::remove(path_.c_str()); }

    const std::string& path() const { return path_; }
private:
    std::string path_;
};

} // anonymous namespace

TEST(MmapReader, OpenSmallFile) {
    std::string content = "<root>hello</root>";
    TempFile tmp(content);

    MmapReader reader;
    reader.open(tmp.path());

    ASSERT_TRUE(reader.is_open());
    ASSERT_EQ(reader.size(), content.size());

    auto data = reader.data();
    ASSERT_EQ(data.size(), content.size());
    EXPECT_EQ(std::string_view(data.data(), data.size()), content);
}

TEST(MmapReader, OpenLargeFile) {
    // Create a file larger than MMAP_THRESHOLD (4096)
    std::string content(8192, 'x');
    content[0] = '<';
    content[content.size() - 1] = '>';
    TempFile tmp(content);

    MmapReader reader;
    reader.open(tmp.path());

    ASSERT_TRUE(reader.is_open());
    ASSERT_EQ(reader.size(), content.size());

    auto data = reader.data();
    EXPECT_EQ(data[0], '<');
    EXPECT_EQ(data[data.size() - 1], '>');
}

TEST(MmapReader, EmptyFile) {
    TempFile tmp("");

    MmapReader reader;
    reader.open(tmp.path());

    EXPECT_EQ(reader.size(), 0u);
    EXPECT_TRUE(reader.data().empty());
}

TEST(MmapReader, NonexistentFile) {
    MmapReader reader;
    EXPECT_THROW(reader.open("/nonexistent/path/to/file.xml"), IOError);
}

TEST(MmapReader, LoadString) {
    std::string content = "<hello/>";
    MmapReader reader;
    reader.load_string(content);

    ASSERT_EQ(reader.size(), content.size());
    EXPECT_EQ(std::string_view(reader.data().data(), reader.data().size()), content);
}

TEST(MmapReader, LoadBuffer) {
    const char* data = "<test>123</test>";
    size_t len = std::strlen(data);

    MmapReader reader;
    reader.load_buffer(data, len);

    ASSERT_EQ(reader.size(), len);
    EXPECT_EQ(std::string_view(reader.data().data(), reader.data().size()),
              std::string_view(data, len));
}

TEST(MmapReader, MoveSemantics) {
    std::string content = "<root/>";
    TempFile tmp(content);

    MmapReader reader1;
    reader1.open(tmp.path());

    MmapReader reader2 = std::move(reader1);
    ASSERT_EQ(reader2.size(), content.size());
    EXPECT_EQ(std::string_view(reader2.data().data(), reader2.data().size()), content);
}

TEST(MmapReader, CloseAndReopen) {
    std::string content1 = "<a/>";
    std::string content2 = "<b>text</b>";
    TempFile tmp1(content1);
    TempFile tmp2(content2);

    MmapReader reader;
    reader.open(tmp1.path());
    EXPECT_EQ(reader.size(), content1.size());

    reader.open(tmp2.path());
    EXPECT_EQ(reader.size(), content2.size());
    EXPECT_EQ(std::string_view(reader.data().data(), reader.data().size()), content2);
}

TEST(MmapReader, DefaultConstructedNotOpen) {
    MmapReader reader;
    EXPECT_FALSE(reader.is_open());
}

TEST(MmapReader, MoveInvalidatesSource) {
    std::string content = "<root/>";
    TempFile tmp(content);

    MmapReader reader1;
    reader1.open(tmp.path());
    EXPECT_TRUE(reader1.is_open());

    MmapReader reader2 = std::move(reader1);
    EXPECT_TRUE(reader2.is_open());
    EXPECT_FALSE(reader1.is_open()); // NOLINT: intentional use-after-move
}

TEST(MmapReader, EmptyFileIsOpen) {
    TempFile tmp("");

    MmapReader reader;
    reader.open(tmp.path());

    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.size(), 0u);
}

TEST(MmapReader, CloseMarksNotOpen) {
    std::string content = "<root/>";
    MmapReader reader;
    reader.load_string(content);
    EXPECT_TRUE(reader.is_open());

    reader.close();
    EXPECT_FALSE(reader.is_open());
}
