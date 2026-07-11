/// @file bench_scaling.cpp
/// @brief Scaling benchmark: 20 sizes from 1 KB to 10 GB.
///
/// Tests parshred (FastSAX turbo/normal, chunked, parallel, SIMD scan)
/// against the best 5 XML parsers: RapidXML, pugixml, libxml2, Expat.
///
/// For large files (> 1 GB), DOM parsers are skipped to avoid OOM.
/// Test data is generated on disk and memory-mapped.
///
/// Compile:
///   g++ -std=c++20 -O3 -march=native -DNDEBUG \
///       -Iinclude -I/tmp/rapidxml/rapidxml-1.13 \
///       bench/bench_scaling.cpp build/release/src/libparshred.a \
///       $(pkg-config --cflags --libs libxml-2.0 expat pugixml) \
///       -lpthread -o bench_scaling

#include <parshred/parshred.hpp>
#include <parshred/fast_sax.hpp>
#include <parshred/pipeline.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ── Comparison parsers ────────────────────────────────────────────────
#include "rapidxml.hpp"
#include <pugixml.hpp>
#include <libxml/parser.h>
#include <libxml/SAX2.h>
#include <expat.h>

// ── Timing ────────────────────────────────────────────────────────────

template<typename T>
void do_not_optimize(T&& value) {
    asm volatile("" : : "g"(value) : "memory");
}

struct TimedResult {
    double seconds;
    size_t elements;
};

template<typename Fn>
TimedResult time_median(Fn&& fn, int runs) {
    std::vector<double> times;
    times.reserve(runs);
    size_t elems = 0;
    for (int i = 0; i < runs; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        elems = fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        times.push_back(std::chrono::duration<double>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], elems};
}

// ── File generation ──────────────────────────────────────────────────

static std::string generate_xml_file(const std::string& path, size_t target_bytes) {
    // Check if file already exists with the right size
    if (std::filesystem::exists(path)) {
        auto sz = std::filesystem::file_size(path);
        if (sz >= target_bytes && sz <= target_bytes * 1.05) {
            return path;
        }
    }

    std::cerr << "  Generating " << path << " (" << target_bytes << " bytes)..." << std::flush;

    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        std::cerr << " FAILED\n";
        return {};
    }

    fputs("<?xml version=\"1.0\"?>\n<root>\n", f);
    size_t written = 30;
    long i = 0;

    // Use a fixed-size buffer for efficiency
    char line[256];
    while (written < target_bytes) {
        int n = snprintf(line, sizeof(line),
            "  <item id=\"%ld\" type=\"bench\" category=\"test\" status=\"active\">"
            "Lorem ipsum dolor sit amet %ld</item>\n", i, i);
        fwrite(line, 1, n, f);
        written += n;
        ++i;
    }

    fputs("</root>\n", f);
    fclose(f);

    auto actual = std::filesystem::file_size(path);
    std::cerr << " done (" << (actual / (1024.0 * 1024.0)) << " MB, "
              << i << " elements)\n";
    return path;
}

// ── Memory-mapped file reader ────────────────────────────────────────

struct MappedFile {
    const char* data = nullptr;
    size_t size = 0;
    int fd = -1;

    bool open(const std::string& path) {
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { ::close(fd); fd = -1; return false; }
        size = st.st_size;
        data = static_cast<const char*>(
            mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0));
        if (data == MAP_FAILED) { data = nullptr; ::close(fd); fd = -1; return false; }
        madvise(const_cast<char*>(data), size, MADV_SEQUENTIAL);
        return true;
    }

    void close() {
        if (data) { munmap(const_cast<char*>(data), size); data = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    ~MappedFile() { close(); }
};

// ── Benchmark runners ────────────────────────────────────────────────

struct BenchResult {
    std::string parser;
    double seconds;
    size_t bytes;
    size_t elements;
    bool skipped;

    double gbps() const { return skipped ? 0.0 : (bytes / 1e9) / seconds; }
    double mbps() const { return skipped ? 0.0 : (bytes / 1e6) / seconds; }
};

static BenchResult run_fastsax_turbo(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        parshred::CountingHandler h;
        parshred::fast_parse_turbo(data, len, h);
        do_not_optimize(h.elements);
        return h.elements;
    }, runs);
    return {"FastSAX turbo", r.seconds, len, r.elements, false};
}

static BenchResult run_fastsax_normal(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        parshred::CountingHandler h;
        parshred::fast_parse(data, len, h);
        do_not_optimize(h.elements);
        return h.elements;
    }, runs);
    return {"FastSAX normal", r.seconds, len, r.elements, false};
}

static BenchResult run_chunked_turbo(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        parshred::CountingHandler h;
        parshred::PipelineConfig cfg;
        cfg.chunk_size = 4 * 1024 * 1024;  // 4 MB chunks
        parshred::ChunkedParser<parshred::ParseMode::Turbo> parser(cfg);
        parser.parse(data, len, h);
        do_not_optimize(h.elements);
        return h.elements;
    }, runs);
    return {"Chunked turbo", r.seconds, len, r.elements, false};
}

static BenchResult run_parallel_turbo(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        parshred::CountingHandler h;
        parshred::PipelineConfig cfg;
        cfg.chunk_size = 4 * 1024 * 1024;
        parshred::ParallelParser<parshred::ParseMode::Turbo> parser(cfg);
        parser.parse(data, len, h);
        do_not_optimize(h.elements);
        return h.elements;
    }, runs);
    return {"Parallel turbo", r.seconds, len, r.elements, false};
}

static BenchResult run_simd_scan(const char* data, size_t len, int runs, size_t max_alloc) {
    // SIMD scan builds a full structural index in memory — skip for large files
    if (len > max_alloc / 4) return {"SIMD scan", 0, len, 0, true};
    auto r = time_median([&]() -> size_t {
        auto idx = parshred::simd_scan({data, len});
        size_t n = idx.positions.size();
        do_not_optimize(n);
        return n;
    }, runs);
    return {"SIMD scan", r.seconds, len, r.elements, false};
}

// RapidXML — parse-only (fastest mode, in-situ, needs writable copy)
static BenchResult run_rapidxml(const char* data, size_t len, int runs, size_t max_alloc) {
    if (len > max_alloc) return {"RapidXML", 0, len, 0, true};
    auto r = time_median([&]() -> size_t {
        std::vector<char> buf(data, data + len);
        buf.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<rapidxml::parse_fastest>(buf.data());
        do_not_optimize(doc);
        // Count elements (cheaply)
        size_t count = 0;
        for (auto* n = doc.first_node(); n; n = n->first_node()) {
            ++count;
            for (auto* s = n->first_node(); s; s = s->next_sibling())
                ++count;
        }
        return count;
    }, runs);
    return {"RapidXML", r.seconds, len, r.elements, false};
}

// pugixml — parse-only (minimal flags)
static BenchResult run_pugixml(const char* data, size_t len, int runs, size_t max_alloc) {
    if (len > max_alloc) return {"pugixml", 0, len, 0, true};
    auto r = time_median([&]() -> size_t {
        pugi::xml_document doc;
        auto result = doc.load_buffer(data, len,
                                       pugi::parse_minimal | pugi::parse_ws_pcdata);
        do_not_optimize(result);
        return result ? 1 : 0;
    }, runs);
    return {"pugixml", r.seconds, len, r.elements, false};
}

// libxml2 SAX2
static void lx2_start_scaling(void* ctx, const xmlChar*, const xmlChar*, const xmlChar*,
                               int, const xmlChar**, int, int, const xmlChar**) {
    (*(size_t*)ctx)++;
}
static void lx2_end_scaling(void*, const xmlChar*, const xmlChar*, const xmlChar*) {}
static void lx2_chars_scaling(void*, const xmlChar*, int) {}

static BenchResult run_libxml2(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        size_t count = 0;
        xmlSAXHandler handler = {};
        handler.initialized = XML_SAX2_MAGIC;
        handler.startElementNs = lx2_start_scaling;
        handler.endElementNs = lx2_end_scaling;
        handler.characters = lx2_chars_scaling;
        xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt(&handler, &count, nullptr, 0, nullptr);
        if (ctx) {
            // Feed in 4 MB chunks (fits comfortably in int)
            const int feed_size = 4 * 1024 * 1024;
            size_t pos = 0;
            while (pos < len) {
                int chunk = static_cast<int>(std::min(static_cast<size_t>(feed_size), len - pos));
                int is_final = (pos + chunk >= len) ? 1 : 0;
                xmlParseChunk(ctx, data + pos, chunk, is_final);
                pos += chunk;
            }
            if (pos < len || len == 0)
                xmlParseChunk(ctx, nullptr, 0, 1);  // finalize if needed
            xmlFreeParserCtxt(ctx);
        }
        do_not_optimize(count);
        return count;
    }, runs);
    return {"libxml2 SAX2", r.seconds, len, r.elements, false};
}

// Expat SAX
static void XMLCALL ex_start_scaling(void* ctx, const XML_Char*, const XML_Char**) {
    (*(size_t*)ctx)++;
}
static void XMLCALL ex_end_scaling(void*, const XML_Char*) {}
static void XMLCALL ex_chars_scaling(void*, const XML_Char*, int) {}

static BenchResult run_expat(const char* data, size_t len, int runs) {
    auto r = time_median([&]() -> size_t {
        size_t count = 0;
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, &count);
        XML_SetElementHandler(parser, ex_start_scaling, ex_end_scaling);
        XML_SetCharacterDataHandler(parser, ex_chars_scaling);
        // Feed in chunks for very large files
        const size_t feed_size = 64 * 1024 * 1024;
        size_t pos = 0;
        while (pos < len) {
            size_t chunk = std::min(feed_size, len - pos);
            int is_final = (pos + chunk >= len) ? XML_TRUE : XML_FALSE;
            XML_Parse(parser, data + pos, static_cast<int>(std::min(chunk, size_t(INT_MAX))), is_final);
            pos += chunk;
        }
        XML_ParserFree(parser);
        do_not_optimize(count);
        return count;
    }, runs);
    return {"Expat SAX", r.seconds, len, r.elements, false};
}

// ── Human-readable size ──────────────────────────────────────────────

static std::string human_size(size_t bytes) {
    char buf[64];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%zu B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024UL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    return buf;
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::cout << R"(
  ╔═══════════════════════════════════════════════════════════════════╗
  ║   Parshred Scaling Benchmark — 20 Sizes from 1 KB to 10 GB      ║
  ║   vs RapidXML, pugixml, libxml2, Expat                          ║
  ╚═══════════════════════════════════════════════════════════════════╝
)" << std::flush;

    // Parse arguments
    std::string data_dir = "/home/gregghy/progetti/parshred/bench/data";
    size_t max_alloc_mb = 3000;  // Max memory for DOM parsers (MB)
    bool csv = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--csv") csv = true;
        else if (arg == "--dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--max-dom-mb" && i + 1 < argc) max_alloc_mb = std::stoull(argv[++i]);
    }

    size_t max_alloc = max_alloc_mb * 1024UL * 1024;
    std::filesystem::create_directories(data_dir);

    // ── 20 test sizes (logarithmically spaced) ──────────────────────
    // From 1 KB to 10 GB in 20 steps
    struct TestSize {
        std::string label;
        size_t bytes;
        int runs;
    };

    std::vector<TestSize> sizes = {
        {"1 KB",      1UL * 1024,                   100},
        {"4 KB",      4UL * 1024,                    80},
        {"16 KB",     16UL * 1024,                   60},
        {"64 KB",     64UL * 1024,                   40},
        {"256 KB",    256UL * 1024,                  30},
        {"1 MB",      1UL * 1024 * 1024,             20},
        {"4 MB",      4UL * 1024 * 1024,             10},
        {"16 MB",     16UL * 1024 * 1024,             5},
        {"64 MB",     64UL * 1024 * 1024,             3},
        {"128 MB",    128UL * 1024 * 1024,            3},
        {"256 MB",    256UL * 1024 * 1024,            3},
        {"512 MB",    512UL * 1024 * 1024,            2},
        {"1 GB",      1UL * 1024 * 1024 * 1024,      2},
        {"1.5 GB",    1536UL * 1024 * 1024,           2},
        {"2 GB",      2UL * 1024 * 1024 * 1024,      2},
        {"3 GB",      3UL * 1024 * 1024 * 1024,      1},
        {"4 GB",      4UL * 1024 * 1024 * 1024,      1},
        {"5 GB",      5UL * 1024 * 1024 * 1024,      1},
        {"7 GB",      7UL * 1024 * 1024 * 1024,      1},
        {"10 GB",     10UL * 1024 * 1024 * 1024,     1},
    };

    // System info
    std::cout << "System: ";
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                std::cout << line.substr(line.find(':') + 2);
                break;
            }
        }
    }
    std::cout << ", " << std::thread::hardware_concurrency() << " threads\n";
    std::cout << "DOM parser limit: " << max_alloc_mb << " MB\n";
    std::cout << "Data directory: " << data_dir << "\n\n";

    // CSV header
    if (csv) {
        std::cout << "size_bytes,size_label,parser,seconds,gbps,mbps,elements,skipped\n";
    }

    // ── Run benchmarks ──────────────────────────────────────────────
    for (auto& sz : sizes) {
        // Generate or reuse test file
        std::string filename = data_dir + "/bench_" + std::to_string(sz.bytes) + ".xml";
        std::string path = generate_xml_file(filename, sz.bytes);
        if (path.empty()) {
            std::cerr << "  SKIP " << sz.label << " — cannot generate file\n";
            continue;
        }

        // Memory-map the file
        MappedFile mf;
        if (!mf.open(path)) {
            std::cerr << "  SKIP " << sz.label << " — cannot mmap file\n";
            continue;
        }

        const char* data = mf.data;
        size_t len = mf.size;

        std::cerr << "Benchmarking " << sz.label << " (" << human_size(len)
                  << ", " << sz.runs << " runs)...\n" << std::flush;

        std::vector<BenchResult> results;

        // Parshred variants
        results.push_back(run_simd_scan(data, len, sz.runs, max_alloc));
        results.push_back(run_fastsax_turbo(data, len, sz.runs));
        results.push_back(run_fastsax_normal(data, len, sz.runs));
        results.push_back(run_chunked_turbo(data, len, sz.runs));
        results.push_back(run_parallel_turbo(data, len, sz.runs));

        // Competition
        results.push_back(run_rapidxml(data, len, sz.runs, max_alloc));
        results.push_back(run_pugixml(data, len, sz.runs, max_alloc));
        results.push_back(run_libxml2(data, len, sz.runs));
        results.push_back(run_expat(data, len, sz.runs));

        // Sort by throughput descending
        std::sort(results.begin(), results.end(),
                  [](const BenchResult& a, const BenchResult& b) {
                      return a.gbps() > b.gbps();
                  });

        if (csv) {
            for (auto& r : results) {
                std::cout << len << ","
                          << sz.label << ","
                          << r.parser << ","
                          << std::fixed << std::setprecision(6) << r.seconds << ","
                          << std::setprecision(4) << r.gbps() << ","
                          << std::setprecision(1) << r.mbps() << ","
                          << r.elements << ","
                          << (r.skipped ? "yes" : "no") << "\n";
            }
        } else {
            // Pretty table
            std::cout << "\n┌─────────────────────────────────────────────────────────────────────────────────────┐\n";
            std::cout << "│ " << std::left << std::setw(83) << (sz.label + " (" + human_size(len) + ")") << "│\n";
            std::cout << "├─────────────────────┬──────────────┬──────────┬──────────┬──────────┬──────────────┤\n";
            std::cout << "│ " << std::left << std::setw(20) << "Parser"
                      << "│" << std::right << std::setw(13) << "Time (s) "
                      << "│" << std::setw(9) << "GB/s "
                      << "│" << std::setw(9) << "MB/s "
                      << "│" << std::setw(9) << "Ratio "
                      << "│" << std::setw(13) << "Elements "
                      << "│\n";
            std::cout << "├─────────────────────┼──────────────┼──────────┼──────────┼──────────┼──────────────┤\n";

            // Find the fastest non-scan parser for ratio
            double ref_gbps = 0;
            for (auto& r : results) {
                if (!r.skipped && r.parser != "SIMD scan" && r.gbps() > ref_gbps)
                    ref_gbps = r.gbps();
            }
            // Find slowest for speedup
            double slowest_gbps = 1e18;
            for (auto& r : results) {
                if (!r.skipped && r.gbps() > 0 && r.gbps() < slowest_gbps)
                    slowest_gbps = r.gbps();
            }

            for (auto& r : results) {
                std::cout << "│ " << std::left << std::setw(20) << r.parser << "│";
                if (r.skipped) {
                    std::cout << std::right << std::setw(13) << "SKIPPED "
                              << "│" << std::setw(9) << "- "
                              << "│" << std::setw(9) << "- "
                              << "│" << std::setw(9) << "- "
                              << "│" << std::setw(13) << "- "
                              << "│\n";
                } else {
                    char time_buf[32];
                    if (r.seconds < 0.001)
                        snprintf(time_buf, sizeof(time_buf), "%.3f ms", r.seconds * 1000);
                    else if (r.seconds < 1.0)
                        snprintf(time_buf, sizeof(time_buf), "%.3f ms", r.seconds * 1000);
                    else
                        snprintf(time_buf, sizeof(time_buf), "%.3f s", r.seconds);

                    char ratio_buf[32];
                    if (slowest_gbps > 0)
                        snprintf(ratio_buf, sizeof(ratio_buf), "%.1fx", r.gbps() / slowest_gbps);
                    else
                        snprintf(ratio_buf, sizeof(ratio_buf), "-");

                    std::cout << std::right
                              << std::setw(13) << time_buf
                              << "│" << std::fixed << std::setprecision(2) << std::setw(9) << r.gbps()
                              << "│" << std::setprecision(0) << std::setw(9) << r.mbps()
                              << "│" << std::setw(9) << ratio_buf
                              << "│" << std::setw(13) << r.elements
                              << "│\n";
                }
            }
            std::cout << "└─────────────────────┴──────────────┴──────────┴──────────┴──────────┴──────────────┘\n";
        }

        mf.close();
        std::cout << std::flush;
    }

    // Summary
    if (!csv) {
        std::cout << "\n"
                  << "═══════════════════════════════════════════════════════════════════════════════════════\n"
                  << "Notes:\n"
                  << "  - SIMD scan: structural character detection only (not a full parser)\n"
                  << "  - FastSAX turbo: fused single-pass SAX, no entity expansion\n"
                  << "  - FastSAX normal: fused single-pass SAX, with entity expansion\n"
                  << "  - Chunked turbo: 4 MB chunks, constant memory overhead\n"
                  << "  - Parallel turbo: chunked + I/O prefetch via madvise\n"
                  << "  - RapidXML: in-situ DOM parser (parse_fastest mode)\n"
                  << "  - pugixml: DOM parser (parse_minimal mode)\n"
                  << "  - libxml2 SAX2: streaming SAX parser (push mode, 64 MB chunks)\n"
                  << "  - Expat SAX: streaming SAX parser (64 MB chunks)\n"
                  << "  - SKIPPED: file too large for DOM parser memory limit\n"
                  << "  - Times are median of N runs\n"
                  << "  - Ratio is vs. the slowest parser at that size\n"
                  << "\n";
    }

    return 0;
}
