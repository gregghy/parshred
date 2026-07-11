/// @file bench_comprehensive.cpp
/// @brief Comprehensive head-to-head benchmark: parshred vs all SOTA XML parsers.
///
/// Compiles standalone:
///   g++ -std=c++20 -O3 -mavx2 -mpclmul \
///       -I../include -I/tmp/rapidxml/rapidxml-1.13 \
///       bench_comprehensive.cpp ../build/release/src/libparshred.a \
///       $(pkg-config --cflags --libs libxml-2.0 expat pugixml) \
///       -o bench_comprehensive

#include <parshred/parshred.hpp>
#include <parshred/fast_sax.hpp>
#include <parshred/pipeline.hpp>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ── Comparison parsers ────────────────────────────────────────────────

// RapidXML (header-only, the classic "fastest XML parser")
#include "rapidxml.hpp"

// pugixml
#include <pugixml.hpp>

// libxml2 SAX2
#include <libxml/parser.h>
#include <libxml/SAX2.h>

// Expat
#include <expat.h>

// ── Timing utility ────────────────────────────────────────────────────

template<typename Fn>
double time_best_ns(Fn&& fn, int runs = 20) {
    double best = 1e18;
    for (int i = 0; i < runs; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        if (ns < best) best = ns;
    }
    return best;
}

template<typename T>
void do_not_optimize(T&& value) {
    asm volatile("" : : "g"(value) : "memory");
}

// ── Load test data ───────────────────────────────────────────────────

std::string load_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string generate_xml(size_t target_size) {
    std::string d;
    d.reserve(target_size + 1024);
    d += "<?xml version=\"1.0\"?>\n<root>\n";
    int i = 0;
    while (d.size() < target_size) {
        d += "  <item id=\"" + std::to_string(i) + "\" type=\"bench\" status=\"active\">"
             "value " + std::to_string(i) + "</item>\n";
        ++i;
    }
    d += "</root>\n";
    return d;
}

// ── Benchmark functions ──────────────────────────────────────────────

struct Result {
    std::string name;
    double ns;
    size_t bytes;
    size_t elements;

    double gbps() const { return bytes / ns; } // bytes/ns = GB/s
    double mbps() const { return (bytes / 1e6) / (ns / 1e9); }
};

Result bench_parshred_scan(const std::string& data, int runs) {
    size_t n = 0;
    double ns = time_best_ns([&]{
        auto idx = parshred::simd_scan({data.data(), data.size()});
        n = idx.positions.size();
        do_not_optimize(n);
    }, runs);
    return {"parshred SIMD scan", ns, data.size(), n};
}

Result bench_parshred_sax(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::SaxParser parser;
        parser.parse_string(data);
        elems = parser.stats().elements;
        do_not_optimize(elems);
    }, runs);
    return {"parshred SAX", ns, data.size(), elems};
}

Result bench_parshred_sax_callbacks(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::SaxParser parser;
        size_t count = 0;
        parser.on_start_element([&](std::string_view, std::span<const parshred::Attribute>) { ++count; });
        parser.on_end_element([&](std::string_view) { ++count; });
        parser.on_text([&](std::string_view) { ++count; });
        parser.parse_string(data);
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"parshred SAX+cb", ns, data.size(), elems};
}

Result bench_fast_sax_turbo(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::CountingHandler handler;
        parshred::fast_parse_turbo(data.data(), data.size(), handler);
        elems = handler.elements;
        do_not_optimize(elems);
    }, runs);
    return {"FastSAX turbo", ns, data.size(), elems};
}

Result bench_fast_sax_normal(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::CountingHandler handler;
        parshred::fast_parse(data.data(), data.size(), handler);
        elems = handler.elements;
        do_not_optimize(elems);
    }, runs);
    return {"FastSAX normal", ns, data.size(), elems};
}

Result bench_fast_sax_null(const std::string& data, int runs) {
    double ns = time_best_ns([&]{
        parshred::NullHandler handler;
        parshred::fast_parse_turbo(data.data(), data.size(), handler);
        do_not_optimize(handler);
    }, runs);
    return {"FastSAX null", ns, data.size(), 0};
}

Result bench_fast_sax_easy(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::FastSaxParserEasy parser;
        size_t count = 0;
        parser.on_start_element([&](std::string_view, std::span<const parshred::Attribute>) { ++count; });
        parser.on_end_element([&](std::string_view) { ++count; });
        parser.on_text([&](std::string_view) { ++count; });
        parser.parse_string(data);
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"FastSAX easy+cb", ns, data.size(), elems};
}

Result bench_chunked_turbo(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::CountingHandler handler;
        parshred::ChunkedParser<parshred::ParseMode::Turbo> parser;
        parser.parse(data.data(), data.size(), handler);
        elems = handler.elements;
        do_not_optimize(elems);
    }, runs);
    return {"Chunked turbo", ns, data.size(), elems};
}

Result bench_parallel_turbo(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        parshred::CountingHandler handler;
        parshred::ParallelParser<parshred::ParseMode::Turbo> parser;
        parser.parse(data.data(), data.size(), handler);
        elems = handler.elements;
        do_not_optimize(elems);
    }, runs);
    return {"Parallel turbo", ns, data.size(), elems};
}

Result bench_rapidxml(const std::string& data, int runs) {
    size_t elems = 0;
    // RapidXML does in-situ parsing (modifies the buffer), so we need a copy each time
    double ns = time_best_ns([&]{
        std::vector<char> buf(data.begin(), data.end());
        buf.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<rapidxml::parse_fastest>(buf.data());
        // Count elements
        size_t count = 0;
        std::function<void(rapidxml::xml_node<>*)> walk = [&](rapidxml::xml_node<>* node) {
            for (auto* n = node->first_node(); n; n = n->next_sibling()) {
                if (n->type() == rapidxml::node_element) ++count;
                walk(n);
            }
        };
        walk(&doc);
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"RapidXML DOM", ns, data.size(), elems};
}

Result bench_rapidxml_parse_only(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        std::vector<char> buf(data.begin(), data.end());
        buf.push_back('\0');
        rapidxml::xml_document<> doc;
        doc.parse<rapidxml::parse_fastest>(buf.data());
        do_not_optimize(doc);
    }, runs);
    return {"RapidXML parse", ns, data.size(), 0};
}

Result bench_pugixml(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        pugi::xml_document doc;
        doc.load_buffer(data.data(), data.size(),
                        pugi::parse_minimal | pugi::parse_ws_pcdata);
        // Count elements
        size_t count = 0;
        std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node node) {
            for (auto child : node.children()) {
                if (child.type() == pugi::node_element) ++count;
                walk(child);
            }
        };
        walk(doc);
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"pugixml DOM", ns, data.size(), elems};
}

Result bench_pugixml_parse_only(const std::string& data, int runs) {
    double ns = time_best_ns([&]{
        pugi::xml_document doc;
        auto r = doc.load_buffer(data.data(), data.size(),
                                  pugi::parse_minimal | pugi::parse_ws_pcdata);
        do_not_optimize(r);
    }, runs);
    return {"pugixml parse", ns, data.size(), 0};
}

// libxml2 SAX2 callbacks
static void lx2_start(void* ctx, const xmlChar*, const xmlChar*, const xmlChar*,
                       int, const xmlChar**, int, int, const xmlChar**) {
    (*(size_t*)ctx)++;
}
static void lx2_end(void*, const xmlChar*, const xmlChar*, const xmlChar*) {}
static void lx2_chars(void*, const xmlChar*, int) {}

Result bench_libxml2(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        size_t count = 0;
        xmlSAXHandler handler = {};
        handler.initialized = XML_SAX2_MAGIC;
        handler.startElementNs = lx2_start;
        handler.endElementNs = lx2_end;
        handler.characters = lx2_chars;

        xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt(&handler, &count, nullptr, 0, nullptr);
        if (ctx) {
            xmlCtxtReadMemory(ctx, data.c_str(), static_cast<int>(data.size()),
                              nullptr, nullptr, 0);
            xmlFreeParserCtxt(ctx);
        }
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"libxml2 SAX2", ns, data.size(), elems};
}

// Expat SAX callbacks
static void XMLCALL ex_start(void* ctx, const XML_Char*, const XML_Char**) {
    (*(size_t*)ctx)++;
}
static void XMLCALL ex_end(void*, const XML_Char*) {}
static void XMLCALL ex_chars(void*, const XML_Char*, int) {}

Result bench_expat(const std::string& data, int runs) {
    size_t elems = 0;
    double ns = time_best_ns([&]{
        size_t count = 0;
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetUserData(parser, &count);
        XML_SetElementHandler(parser, ex_start, ex_end);
        XML_SetCharacterDataHandler(parser, ex_chars);
        XML_Parse(parser, data.c_str(), static_cast<int>(data.size()), XML_TRUE);
        XML_ParserFree(parser);
        elems = count;
        do_not_optimize(elems);
    }, runs);
    return {"Expat SAX", ns, data.size(), elems};
}

// ── Main ──────────────────────────────────────────────────────────────

void print_results(const std::string& label, std::vector<Result>& results) {
    // Sort by throughput descending
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) { return a.gbps() > b.gbps(); });

    double best = results[0].gbps();

    std::cout << "\n=== " << label << " (" << (results[0].bytes / 1024)
              << " KB) ===\n\n";
    std::cout << std::left << std::setw(22) << "Parser"
              << std::right << std::setw(12) << "Time (ms)"
              << std::setw(12) << "GB/s"
              << std::setw(12) << "MB/s"
              << std::setw(10) << "Speedup"
              << std::setw(12) << "Elements"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (auto& r : results) {
        std::cout << std::left << std::setw(22) << r.name
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(12) << (r.ns / 1e6)
                  << std::setprecision(2)
                  << std::setw(12) << r.gbps()
                  << std::setprecision(0)
                  << std::setw(12) << r.mbps()
                  << std::setprecision(2)
                  << std::setw(9) << (r.gbps() / results.back().gbps()) << "x"
                  << std::setw(12) << r.elements
                  << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << R"(
  ╔═══════════════════════════════════════════════════════════════╗
  ║   Parshred Comprehensive XML Parser Benchmark               ║
  ║   vs RapidXML, pugixml, libxml2, Expat                      ║
  ╚═══════════════════════════════════════════════════════════════╝
)";

    // Determine data source
    std::string bench_dir;
    if (argc >= 2) {
        bench_dir = argv[1];
        if (bench_dir.back() != '/') bench_dir += '/';
    }

    // Test sizes
    struct TestCase { std::string name; size_t size; int runs; };
    std::vector<TestCase> cases = {
        {"Small (~1 KB)", 1024, 50},
        {"Medium (~1 MB)", 1'000'000, 20},
        {"Large (~10 MB)", 10'000'000, 5},
    };

    for (auto& tc : cases) {
        std::string data;
        if (!bench_dir.empty()) {
            if (tc.size <= 2000) data = load_file(bench_dir + "small.xml");
            else if (tc.size <= 2'000'000) data = load_file(bench_dir + "medium.xml");
            else data = load_file(bench_dir + "large.xml");
        }
        if (data.empty()) {
            data = generate_xml(tc.size);
        }

        std::vector<Result> results;
        results.push_back(bench_parshred_scan(data, tc.runs));
        results.push_back(bench_fast_sax_null(data, tc.runs));
        results.push_back(bench_fast_sax_turbo(data, tc.runs));
        results.push_back(bench_fast_sax_normal(data, tc.runs));
        results.push_back(bench_fast_sax_easy(data, tc.runs));
        results.push_back(bench_chunked_turbo(data, tc.runs));
        results.push_back(bench_parallel_turbo(data, tc.runs));
        results.push_back(bench_parshred_sax(data, tc.runs));
        results.push_back(bench_parshred_sax_callbacks(data, tc.runs));
        results.push_back(bench_rapidxml_parse_only(data, tc.runs));
        results.push_back(bench_rapidxml(data, tc.runs));
        results.push_back(bench_pugixml_parse_only(data, tc.runs));
        results.push_back(bench_pugixml(data, tc.runs));
        results.push_back(bench_libxml2(data, tc.runs));
        results.push_back(bench_expat(data, tc.runs));

        print_results(tc.name, results);
    }

    std::cout << "\nNotes:\n"
              << "  - 'parse only' = parse into DOM, no traversal\n"
              << "  - 'SIMD scan' = structural character detection only (stage 1)\n"
              << "  - All times are best-of-N to minimize noise\n"
              << "  - Speedup is relative to the slowest parser\n"
              << "\n";

    return 0;
}
