/// @file bench_sax.cpp
/// @brief Benchmark: full SAX parsing throughput.

#include <benchmark/benchmark.h>
#include <parshred/sax_parser.hpp>

#include <fstream>
#include <sstream>
#include <string>

#ifndef BENCH_DATA_DIR
#define BENCH_DATA_DIR "./data/"
#endif

namespace {

std::string load_or_generate(const std::string& name, size_t fallback_size = 65536) {
    std::string path = std::string(BENCH_DATA_DIR) + name;
    std::ifstream ifs(path, std::ios::binary);
    if (ifs) {
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }
    std::string data;
    data.reserve(fallback_size);
    data += "<root>\n";
    while (data.size() < fallback_size - 20) {
        data += R"(<item id=")" + std::to_string(data.size()) + R"(">value</item>)" + "\n";
    }
    data += "</root>\n";
    return data;
}

} // namespace

static void BM_SaxSmall(benchmark::State& state) {
    auto data = load_or_generate("small.xml", 1024);
    for (auto _ : state) {
        parshred::SaxParser parser;
        parser.parse_string(data);
        auto s = parser.stats();
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_SaxSmall);

static void BM_SaxMedium(benchmark::State& state) {
    auto data = load_or_generate("medium.xml", 1'000'000);
    for (auto _ : state) {
        parshred::SaxParser parser;
        parser.parse_string(data);
        auto s = parser.stats();
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_SaxMedium);

static void BM_SaxLarge(benchmark::State& state) {
    auto data = load_or_generate("large.xml", 100'000'000);
    for (auto _ : state) {
        parshred::SaxParser parser;
        parser.parse_string(data);
        auto s = parser.stats();
        benchmark::DoNotOptimize(s);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_SaxLarge)->Iterations(3);

static void BM_SaxWithCallbacks(benchmark::State& state) {
    auto data = load_or_generate("medium.xml", 1'000'000);
    for (auto _ : state) {
        parshred::SaxParser parser;
        size_t count = 0;
        parser.on_start_element([&](std::string_view, std::span<const parshred::Attribute>) {
            ++count;
        });
        parser.on_text([&](std::string_view) { ++count; });
        parser.parse_string(data);
        benchmark::DoNotOptimize(count);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_SaxWithCallbacks);
