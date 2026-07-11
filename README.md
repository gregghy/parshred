# Parshred — The World's Fastest XML Parser

**SIMD-accelerated, zero-copy XML parsing for modern hardware.**

Parshred redesigns XML processing around modern CPUs: AVX-512/AVX2 structural scanning, memory-mapped I/O, and streaming SAX parsing — delivering multi-GB/s throughput on commodity hardware.

## Quick Start

### Build

```bash
# Debug build with tests
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure

# Release build with benchmarks
cmake --preset release
cmake --build build/release
```

### CLI

```bash
./build/release/parshred --validate document.xml
./build/release/parshred --count document.xml
./build/release/parshred --bench document.xml
```

### C++ API

```cpp
#include <parshred/parshred.hpp>

parshred::SaxParser parser;

parser.on_start_element([](std::string_view name, std::span<const parshred::Attribute> attrs) {
    std::cout << "<" << name << ">\n";
});

parser.on_text([](std::string_view text) {
    std::cout << text;
});

parser.parse_file("data.xml");

// Check stats
auto& stats = parser.stats();
std::cout << stats.elements << " elements parsed at "
          << (stats.bytes_parsed / 1e9) << " GB\n";
```

### Python API

```bash
pip install -e .
```

```python
import parshred

# Event-driven
for event_type, name, attrs in parshred.iterparse("data.xml"):
    if event_type == "start":
        print(f"<{name}>", attrs)

# SAX-style
parser = parshred.SaxParser()
parser.on_start_element(lambda name, attrs: print(f"<{name}>"))
parser.parse_file("data.xml")
```

## Architecture

```
XML Input → mmap → SIMD Structural Scan → Token Stream → SAX Events
                   (AVX-512/AVX2/SSE4.2)
```

1. **Memory-mapped I/O**: Zero-copy file access via `mmap(2)`
2. **SIMD structural scanner**: Scans 64 bytes/cycle (AVX-512) for `< > / " ' = &` with carry-less multiply quote masking
3. **Tokenizer**: Walks the structural index to produce zero-copy tokens
4. **SAX parser**: Fires callbacks with `string_view` references — no allocations

## Performance

Targets:
- **5x faster** than libxml2
- **>10 GB/s** raw scanning throughput
- **Near-zero** heap allocations during parsing

Run benchmarks:
```bash
cmake --preset release
cmake --build build/release
./build/release/bench/parshred_bench --benchmark_format=console
```

## Requirements

- C++20 compiler (GCC 12+, Clang 15+)
- CMake 3.20+
- x86-64 CPU (SSE 4.2 minimum; AVX2/AVX-512 for best performance)
- Python 3.9+ (for bindings)

## License

MIT — see [LICENSE](LICENSE).
