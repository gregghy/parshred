/// @file bench_dom.cpp
/// @brief Benchmark: Parshred DOM vs RapidXML vs pugixml on small-to-medium files.
///
/// This is the critical benchmark — we need to beat RapidXML at ALL sizes.
/// Compile: g++ -std=c++20 -O3 -mavx2 -I../include -I/tmp/rapidxml/rapidxml-1.13
///          -I/tmp/pugixml/pugixml-1.14/src bench_dom.cpp -o bench_dom
///          -L../build/release/src -lparshred

#include <parshred/dom_parser.hpp>
#include <parshred/dom_fast.hpp>
#include <parshred/fast_sax.hpp>
#include <parshred/mmap_reader.hpp>

// RapidXML
#include <rapidxml.hpp>

// pugixml
#include <pugixml.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

// ── XML Generation ───────────────────────────────────────────────────

static std::string generate_xml(size_t target_bytes) {
    std::string xml;
    xml.reserve(target_bytes + 1024);
    xml += "<?xml version=\"1.0\"?>\n<root>\n";
    int id = 0;
    while (xml.size() < target_bytes - 100) {
        xml += "  <item id=\"" + std::to_string(id) +
               "\" name=\"value" + std::to_string(id) +
               "\" status=\"active\">" +
               "Some text content for item " + std::to_string(id) +
               "</item>\n";
        ++id;
    }
    xml += "</root>\n";
    return xml;
}

// ── Benchmarking Helpers ─────────────────────────────────────────────

struct BenchResult {
    const char* name;
    double      mb_per_sec;
    size_t      elements;
};

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// ── Parsers ──────────────────────────────────────────────────────────

static BenchResult bench_parshred_fast_dom(const char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        auto dom = parshred::fast_dom_parse<parshred::FDOM_FASTEST>(data, len);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) elem_count = dom.node_count;
    }
    double med = median(times);
    return {"Parshred FastDOM", (len / 1e6) / med, elem_count};
}

static BenchResult bench_parshred_dom_fastest(char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        auto result = parshred::dom_parse<parshred::DOM_FASTEST>(data, len);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) elem_count = result.pool.size();
    }
    double med = median(times);
    return {"Parshred DOM fastest", (len / 1e6) / med, elem_count};
}

static BenchResult bench_parshred_dom_default(const char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        auto result = parshred::dom_parse<parshred::DOM_DEFAULT>(data, len);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) elem_count = result.pool.size();
    }
    double med = median(times);
    return {"Parshred DOM default", (len / 1e6) / med, elem_count};
}

static BenchResult bench_parshred_sax_turbo(const char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    for (int i = 0; i < runs; ++i) {
        parshred::CountingHandler h;
        auto t0 = Clock::now();
        parshred::fast_parse_turbo(data, len, h);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) elem_count = h.elements;
    }
    double med = median(times);
    return {"Parshred SAX turbo", (len / 1e6) / med, elem_count};
}

static BenchResult bench_rapidxml(char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    // RapidXML needs null-terminated input
    data[len] = '\0';
    for (int i = 0; i < runs; ++i) {
        rapidxml::xml_document<> doc;
        auto t0 = Clock::now();
        doc.parse<rapidxml::parse_fastest>(data);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) {
            // Count elements
            std::function<size_t(rapidxml::xml_node<>*)> count_nodes;
            count_nodes = [&](rapidxml::xml_node<>* n) -> size_t {
                size_t c = 0;
                for (auto* child = n->first_node(); child; child = child->next_sibling()) {
                    ++c;
                    c += count_nodes(child);
                }
                return c;
            };
            elem_count = count_nodes(&doc) + 1;
        }
    }
    double med = median(times);
    return {"RapidXML fastest", (len / 1e6) / med, elem_count};
}

static BenchResult bench_pugixml(const char* data, size_t len, int runs) {
    std::vector<double> times;
    size_t elem_count = 0;
    for (int i = 0; i < runs; ++i) {
        pugi::xml_document doc;
        auto t0 = Clock::now();
        doc.load_buffer(data, len, pugi::parse_minimal);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(sec);
        if (i == 0) {
            struct Counter : pugi::xml_tree_walker {
                size_t count = 0;
                bool for_each(pugi::xml_node& node) override { ++count; return true; }
            } counter;
            doc.traverse(counter);
            elem_count = counter.count;
        }
    }
    double med = median(times);
    return {"pugixml minimal", (len / 1e6) / med, elem_count};
}

// ── Main ─────────────────────────────────────────────────────────────

int main() {
    printf("\n");
    printf("  ╔═══════════════════════════════════════════════════════════╗\n");
    printf("  ║   Parshred DOM Benchmark vs RapidXML / pugixml          ║\n");
    printf("  ╚═══════════════════════════════════════════════════════════╝\n\n");

    // Test sizes from 512 bytes to 128 MB
    struct TestSize {
        size_t bytes;
        int    runs;
        const char* label;
    };
    std::vector<TestSize> sizes = {
        {       512,  200, "512 B"},
        {      1024,  200, "1 KB"},
        {      4096,  150, "4 KB"},
        {     16384,  100, "16 KB"},
        {     65536,   50, "64 KB"},
        {    262144,   30, "256 KB"},
        {   1048576,   20, "1 MB"},
        {   4194304,   10, "4 MB"},
        {  16777216,    5, "16 MB"},
        {  67108864,    3, "64 MB"},
        { 134217728,    2, "128 MB"},
    };

    printf("%-8s  %-16s %-16s %-16s %-16s %-16s %-16s\n",
           "Size", "FastDOM", "DOM fastest", "SAX turbo",
           "RapidXML", "pugixml", "Ratio vs Rapid");
    printf("%-8s  %-16s %-16s %-16s %-16s %-16s %-16s\n",
           "--------", "----------------", "----------------", "----------------",
           "----------------", "----------------", "----------------");

    for (const auto& sz : sizes) {
        std::string xml = generate_xml(sz.bytes);
        // Make a mutable copy with extra byte for null terminator
        std::vector<char> buf(xml.begin(), xml.end());
        buf.push_back('\0');
        char* data = buf.data();
        size_t len = buf.size() - 1;

        auto r0 = bench_parshred_fast_dom(data, len, sz.runs);
        auto r1 = bench_parshred_dom_fastest(data, len, sz.runs);
        auto r3 = bench_parshred_sax_turbo(data, len, sz.runs);
        auto r4 = bench_rapidxml(data, len, sz.runs);
        auto r5 = bench_pugixml(data, len, sz.runs);

        double best_parshred = std::max({r0.mb_per_sec, r1.mb_per_sec, r3.mb_per_sec});
        double ratio = best_parshred / r4.mb_per_sec;

        printf("%-8s  %7.0f MB/s    %7.0f MB/s    %7.0f MB/s    %7.0f MB/s    %7.0f MB/s    %.2fx\n",
               sz.label, r0.mb_per_sec, r1.mb_per_sec, r3.mb_per_sec,
               r4.mb_per_sec, r5.mb_per_sec, ratio);
    }

    printf("\n");
    return 0;
}
