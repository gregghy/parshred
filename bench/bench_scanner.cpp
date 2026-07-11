/// @file bench_scanner.cpp
/// @brief Benchmark: raw SIMD scanning throughput.

#include <benchmark/benchmark.h>
#include <parshred/simd_scanner.hpp>

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
    // Generate fallback
    std::string data;
    data.reserve(fallback_size);
    while (data.size() < fallback_size) {
        data += R"(<item id=")" + std::to_string(data.size()) + R"(" type="bench">value</item>)";
        data += '\n';
    }
    return data;
}

} // namespace

static void BM_ScanSmall(benchmark::State& state) {
    auto data = load_or_generate("small.xml", 1024);
    for (auto _ : state) {
        auto idx = parshred::simd_scan({data.data(), data.size()});
        benchmark::DoNotOptimize(idx);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ScanSmall);

static void BM_ScanMedium(benchmark::State& state) {
    auto data = load_or_generate("medium.xml", 1'000'000);
    for (auto _ : state) {
        auto idx = parshred::simd_scan({data.data(), data.size()});
        benchmark::DoNotOptimize(idx);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ScanMedium);

static void BM_ScanLarge(benchmark::State& state) {
    auto data = load_or_generate("large.xml", 100'000'000);
    for (auto _ : state) {
        auto idx = parshred::simd_scan({data.data(), data.size()});
        benchmark::DoNotOptimize(idx);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ScanLarge)->Iterations(5);

static void BM_ScanAttrs(benchmark::State& state) {
    auto data = load_or_generate("attrs.xml", 500'000);
    for (auto _ : state) {
        auto idx = parshred::simd_scan({data.data(), data.size()});
        benchmark::DoNotOptimize(idx);
    }
    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(BM_ScanAttrs);
