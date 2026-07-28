/// @file parshred_cli.cpp
/// @brief Command-line interface for parshred.
///
/// Usage:
///   parshred [--validate|--count|--bench] <file.xml>
///   parshred --help

#include <parshred/parshred.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

template<typename T>
inline void do_not_optimize(T&& value) {
#if defined(_MSC_VER)
    // MSVC has no inline-asm escape for this; use the compiler fence +
    // volatile sink pattern instead.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    volatile char sink = reinterpret_cast<const char*>(&value)[0];
    (void)sink;
#else
    asm volatile("" : : "g"(value) : "memory");
#endif
}

static void print_usage() {
    std::cout << R"(
  ╔═══════════════════════════════════════════════════════╗
  ║   parshred — The World's Fastest XML Parser          ║
  ║   v0.1.0                                             ║
  ╚═══════════════════════════════════════════════════════╝

  Usage:
    parshred <file.xml>                   Validate and print stats
    parshred --validate <file.xml>        Validate well-formedness
    parshred --count <file.xml>           Count elements, attributes, etc.
    parshred --bench <file.xml>           Benchmark parsing speed
    parshred --help                       Show this help

)";
}

static void cmd_validate(const std::string& path) {
    try {
        parshred::SaxParser parser;
        parser.parse_file(path);
        std::cout << "✓ " << path << " is well-formed XML\n";
        std::cout << "  " << parser.stats().elements << " elements, "
                  << parser.stats().attributes << " attributes, "
                  << parser.stats().bytes_parsed << " bytes\n";
    } catch (const parshred::ParseError& e) {
        std::cerr << "✗ Parse error at byte " << e.offset() << ": " << e.what() << "\n";
    } catch (const parshred::IOError& e) {
        std::cerr << "✗ I/O error: " << e.what() << "\n";
    }
}

static void cmd_count(const std::string& path) {
    try {
        parshred::SaxParser parser;
        parser.parse_file(path);
        const auto& s = parser.stats();
        std::cout << "File:       " << path << "\n"
                  << "Size:       " << s.bytes_parsed << " bytes\n"
                  << "Elements:   " << s.elements << "\n"
                  << "Attributes: " << s.attributes << "\n"
                  << "Text nodes: " << s.text_nodes << "\n"
                  << "Comments:   " << s.comments << "\n"
                  << "CDATA:      " << s.cdata_nodes << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

static void cmd_bench(const std::string& path) {
    try {
        // First pass: warm up and get file size
        parshred::MmapReader reader;
        reader.open(path);
        auto data = reader.data();
        size_t file_size = data.size();

        std::cout << "Benchmarking: " << path << " (" << file_size << " bytes)\n\n";

        // Benchmark SIMD scan
        {
            constexpr int RUNS = 10;
            auto best = std::chrono::nanoseconds::max();
            for (int i = 0; i < RUNS; ++i) {
                auto t0 = std::chrono::high_resolution_clock::now();
                auto idx = parshred::simd_scan(data);
                auto t1 = std::chrono::high_resolution_clock::now();
                do_not_optimize(idx);
                auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
                if (dt < best) best = dt;
            }
            double secs = best.count() / 1e9;
            double gbps = (file_size / 1e9) / secs;
            std::cout << "  SIMD scan:    " << (best.count() / 1e6) << " ms  ("
                      << gbps << " GB/s)\n";
        }

        // Benchmark full SAX parse
        {
            constexpr int RUNS = 10;
            auto best = std::chrono::nanoseconds::max();
            size_t elements = 0;
            for (int i = 0; i < RUNS; ++i) {
                auto t0 = std::chrono::high_resolution_clock::now();
                parshred::SaxParser parser;
                parser.parse(data);
                auto t1 = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0);
                if (dt < best) {
                    best = dt;
                    elements = parser.stats().elements;
                }
            }
            double secs = best.count() / 1e9;
            double gbps = (file_size / 1e9) / secs;
            std::cout << "  SAX parse:    " << (best.count() / 1e6) << " ms  ("
                      << gbps << " GB/s, " << elements << " elements)\n";
        }

        std::cout << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

// (do_not_optimize template defined above)

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string arg1 = argv[1];

    if (arg1 == "--help" || arg1 == "-h") {
        print_usage();
        return 0;
    }

    if (arg1 == "--validate" && argc >= 3) {
        cmd_validate(argv[2]);
        return 0;
    }

    if (arg1 == "--count" && argc >= 3) {
        cmd_count(argv[2]);
        return 0;
    }

    if (arg1 == "--bench" && argc >= 3) {
        cmd_bench(argv[2]);
        return 0;
    }

    // Default: validate and count
    cmd_validate(arg1);
    return 0;
}
