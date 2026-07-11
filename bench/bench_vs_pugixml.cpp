/// @file bench_vs_pugixml.cpp
/// @brief Benchmark: parshred SAX vs pugixml DOM parse.

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

#ifdef HAS_PUGIXML
#include <pugixml.hpp>

static void BM_Pugixml(benchmark::State& state) {
    auto data = load_or_generate("medium.xml");
    for (auto _ : state) {
        pugi::xml_document doc;
        auto result = doc.load_buffer(data.data(), data.size(),
                                       pugi::parse_default | pugi::parse_ws_pcdata);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_Pugixml);
#endif

static void BM_ParshredSax_VsPugixml(benchmark::State& state) {
    auto data = load_or_generate("medium.xml");
    for (auto _ : state) {
        parshred::SaxParser parser;
        parser.parse_string(data);
        auto s = parser.stats();
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ParshredSax_VsPugixml);
