/// @file bench_vs_libxml2.cpp
/// @brief Benchmark: parshred SAX vs libxml2 SAX.

#include <benchmark/benchmark.h>
#include <parshred/sax_parser.hpp>

#include <fstream>
#include <sstream>
#include <string>

#ifndef BENCH_DATA_DIR
#define BENCH_DATA_DIR "./data/"
#endif

namespace {
std::string load_or_generate(const std::string& name, size_t sz = 1'000'000) {
    std::string path = std::string(BENCH_DATA_DIR) + name;
    std::ifstream ifs(path, std::ios::binary);
    if (ifs) { std::ostringstream o; o << ifs.rdbuf(); return o.str(); }
    std::string d; d.reserve(sz);
    d += "<root>\n";
    while (d.size() < sz - 20)
        d += R"(<item id=")" + std::to_string(d.size()) + R"(">value</item>)" "\n";
    d += "</root>\n";
    return d;
}
} // namespace

#ifdef HAS_LIBXML2
#include <libxml/parser.h>
#include <libxml/SAX2.h>

// SAX2 callback signatures matching libxml2's typedefs
static void lx2_startElementNs(void*, const xmlChar*, const xmlChar*, const xmlChar*,
                                int, const xmlChar**, int, int, const xmlChar**) {}
static void lx2_endElementNs(void*, const xmlChar*, const xmlChar*, const xmlChar*) {}
static void lx2_characters(void*, const xmlChar*, int) {}

static void BM_Libxml2Sax(benchmark::State& state) {
    auto data = load_or_generate("medium.xml");

    for (auto _ : state) {
        xmlSAXHandler handler = {};
        handler.initialized = XML_SAX2_MAGIC;
        handler.startElementNs = lx2_startElementNs;
        handler.endElementNs = lx2_endElementNs;
        handler.characters = lx2_characters;

        xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt(&handler, nullptr, nullptr, 0, nullptr);
        if (ctx) {
            xmlCtxtReadMemory(ctx, data.c_str(), static_cast<int>(data.size()), nullptr, nullptr, 0);
            xmlFreeParserCtxt(ctx);
        }
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_Libxml2Sax);
#endif

static void BM_ParshredSax_VsLibxml2(benchmark::State& state) {
    auto data = load_or_generate("medium.xml");
    for (auto _ : state) {
        parshred::SaxParser parser;
        size_t count = 0;
        parser.on_start_element([&](std::string_view, std::span<const parshred::Attribute>) { ++count; });
        parser.on_end_element([&](std::string_view) { ++count; });
        parser.on_text([&](std::string_view) { ++count; });
        parser.parse_string(data);
        benchmark::DoNotOptimize(count);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ParshredSax_VsLibxml2);
