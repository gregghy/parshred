/// @file bench_main.cpp
/// @brief Benchmark main — Google Benchmark entry point.
///        Individual benchmarks are in separate files.

// Google Benchmark provides its own main via benchmark::benchmark_main
// This file exists for any shared benchmark utilities.

#include <benchmark/benchmark.h>
#include <parshred/mmap_reader.hpp>
#include <string>
#include <fstream>
#include <sstream>

#ifndef BENCH_DATA_DIR
#define BENCH_DATA_DIR "./data/"
#endif

namespace bench {

/// Load a benchmark data file into a string.
inline std::string load_file(const std::string& name) {
    std::string path = std::string(BENCH_DATA_DIR) + name;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        // Generate inline if file doesn't exist
        std::string fallback;
        fallback += "<?xml version=\"1.0\"?>\n<root>\n";
        for (int i = 0; i < 1000; ++i) {
            fallback += "  <item id=\"" + std::to_string(i) + "\">value</item>\n";
        }
        fallback += "</root>\n";
        return fallback;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

} // namespace bench
