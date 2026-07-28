/// @file bench_regression.cpp
/// @brief Self-contained parshred throughput benchmark for CI regression
///        gating. Emits JSON to stdout. No external parser dependencies.
///
/// Measures fast_dom_parse<0> (full-feature FastDOM) at fixed sizes and
/// reports MB/s (median of N runs). The CI workflow compares this against a
/// committed baseline (bench/baseline.json) and fails on >10% regression.
///
/// Usage:
///   bench_regression [--runs N] [--sizes S1,S2,...] [--out file.json]
///
/// Default runs: 7. Default sizes (bytes): 65536,1048576,16777216,67108864.

#include <parshred/dom_fast.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

static std::string generate_xml(size_t target_bytes) {
    std::string xml;
    xml.reserve(target_bytes + 1024);
    xml += "<?xml version=\"1.0\"?>\n<root>\n";
    int id = 0;
    while (xml.size() < target_bytes - 100) {
        xml += "  <item id=\"" + std::to_string(id) +
               "\" name=\"value" + std::to_string(id) +
               "\" status=\"active\">Some text content for item " +
               std::to_string(id) + "</item>\n";
        ++id;
    }
    xml += "</root>\n";
    return xml;
}

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    if (v.size() % 2) return v[v.size() / 2];
    return (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2.0;
}

int main(int argc, char** argv) {
    unsigned runs = 7;
    std::vector<size_t> sizes = {65536, 1048576, 16777216, 67108864};
    std::string out_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--runs" && i + 1 < argc) { runs = unsigned(std::atoi(argv[++i])); continue; }
        if (a == "--out" && i + 1 < argc) { out_path = argv[++i]; continue; }
        if (a == "--sizes" && i + 1 < argc) {
            sizes.clear();
            std::string s = argv[++i];
            std::stringstream ss(s);
            std::string tok;
            while (std::getline(ss, tok, ',')) sizes.push_back(std::stoull(tok));
            continue;
        }
        std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
        return 2;
    }

    std::ostringstream json;
    json << "{\n  \"benchmark\": \"fast_dom_parse\",\n  \"runs\": " << runs
         << ",\n  \"results\": [\n";
    for (size_t si = 0; si < sizes.size(); ++si) {
        size_t target = sizes[si];
        std::string xml = generate_xml(target);
        size_t actual = xml.size();
        std::vector<double> mbps;
        mbps.reserve(runs);
        for (unsigned r = 0; r < runs; ++r) {
            auto t0 = Clock::now();
            auto dom = parshred::fast_dom_parse<0>(xml.data(), xml.size());
            auto t1 = Clock::now();
            double secs = std::chrono::duration<double>(t1 - t0).count();
            mbps.push_back(actual / secs / (1024.0 * 1024.0));
            // Prevent the compiler from optimizing the parse away.
            if (dom.node_count == 0) std::abort();
        }
        double med = median(mbps);
        double min = *std::min_element(mbps.begin(), mbps.end());
        double max = *std::max_element(mbps.begin(), mbps.end());
        json << "    {\"size_bytes\": " << actual
             << ", \"size_label\": \"" << actual / 1024 << "K\""
             << ", \"mbps_median\": " << med
             << ", \"mbps_min\": " << min
             << ", \"mbps_max\": " << max << "}";
        if (si + 1 < sizes.size()) json << ",";
        json << "\n";
        std::fprintf(stderr, "%zu bytes: %.0f MB/s (min %.0f, max %.0f)\n",
                     actual, med, min, max);
    }
    json << "  ]\n}\n";

    if (out_path.empty()) {
        std::printf("%s", json.str().c_str());
    } else {
        std::ofstream f(out_path);
        f << json.str();
        std::fprintf(stderr, "wrote %s\n", out_path.c_str());
    }
    return 0;
}
