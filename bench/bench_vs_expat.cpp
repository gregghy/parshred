/// @file bench_vs_expat.cpp
/// @brief Benchmark: parshred SAX vs Expat SAX.

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

#ifdef HAS_EXPAT
#include <expat.h>

static void XMLCALL expat_start(void*, const XML_Char*, const XML_Char**) {}
static void XMLCALL expat_end(void*, const XML_Char*) {}
static void XMLCALL expat_chars(void*, const XML_Char*, int) {}

static void BM_Expat(benchmark::State& state) {
    auto data = load_or_generate("medium.xml");
    for (auto _ : state) {
        XML_Parser parser = XML_ParserCreate(nullptr);
        XML_SetElementHandler(parser, expat_start, expat_end);
        XML_SetCharacterDataHandler(parser, expat_chars);
        XML_Parse(parser, data.c_str(), static_cast<int>(data.size()), XML_TRUE);
        XML_ParserFree(parser);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_Expat);
#endif

static void BM_ParshredSax_VsExpat(benchmark::State& state) {
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
BENCHMARK(BM_ParshredSax_VsExpat);
