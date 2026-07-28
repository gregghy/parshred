# Parshred

[![CI](https://github.com/parshred/parshred/actions/workflows/ci.yml/badge.svg)](https://github.com/parshred/parshred/actions/workflows/ci.yml)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)

High-performance SIMD-accelerated XML parser for C++20 and Python.

Parshred is built from the ground up for modern CPUs: it scans XML structure with
AVX-512/AVX2/SSE4.2/NEON, keeps data in zero-copy `string_view` ranges, and offers
a compact 32-byte DOM node. The result is a small, fast, standards-focused library
that works well for both C++ and Python applications.

## Feature Highlights

- **2.5–2.7 GB/s DOM parsing** on x86-64 with AVX2; **matches RapidXML at
  small/mid sizes** (within ~5%) and is **1.5–2.4x faster at 64 MB+**, where
  RapidXML's allocation pattern thrashes. Parshred's compact 32-byte DOM
  node keeps the large-file advantage consistent across machines.
- **5–8x faster than lxml** in Python, with competitive or better memory
  efficiency.
- **Multiple APIs**: DOM, SAX, Pull Parser (StAX-style), and a streaming pipeline for
  files larger than memory.
- **XPath 1.0** with full operator, function, and predicate support.
- **XML Namespace 1.0**, internal DTD validation, and XSD simple type validation.
- **SIMD acceleration**: AVX-512, AVX2, SSE4.2, and ARM NEON with runtime dispatch.
- **32-byte compact DOM nodes** — roughly 3x less memory than traditional node-based
  parsers.
- **573 unit tests**, fuzz-tested, and run against the official **W3C XML
  Conformance Test Suite** in CI (results published as a workflow artifact;
  see [Conformance](docs/conformance.md) for the current pass/fail breakdown
  by category).

## Performance

All numbers below are **measured, not projected**. They vary by CPU, memory
subsystem, and compiler; rerun `bench/bench_dom` and `bench/bench_python.py`
on your target hardware for accurate figures. See
[`docs/performance.md`](docs/performance.md) for methodology and the
benchmark-regression CI job that guards against perf regressions.

### C++ DOM throughput (x86-64, AVX2)

Reference machine: AMD Ryzen 5 7640U, 12 threads, GCC 16.1 `-O3`, single
thread. Median of repeated runs; `bench/bench_dom`.

| Size    | Parshred (FastDOM) | RapidXML | pugixml | vs RapidXML |
|---------|--------------------|----------|---------|-------------|
| 64 KB   | 2572 MB/s          | 2477     | 1465    | 1.04x       |
| 1 MB    | 2722 MB/s          | 2757     | 821     | 0.99x       |
| 16 MB   | 2519 MB/s          | 2421     | 1585    | 1.04x       |
| 64 MB   | 1678 MB/s          | 915      | 641     | 1.83x       |
| 128 MB  | 1317 MB/s          | 866      | 712     | 1.52x       |

**Reading the table:** at 64 KB–16 MB both parsers are bound by the same
scalar setup and run within ~5% of each other — Parshred does not claim a
win there. The gap opens at 64 MB+ because RapidXML allocates per-node on
the heap while Parshred packs nodes into a single malloc'd array, so cache
behavior and allocator pressure diverge. On machines with more cache or a
faster allocator the large-file ratio trends higher (we have observed up to
2.4x); on this laptop it is 1.5–1.8x.

### Python throughput

Reference machine: as above, Python 3.14, lxml 6.1.1, single thread. Median
of 3 runs; `bench/bench_python.py`.

| Size   | Parshred | lxml (DOM) | lxml (iterparse) | vs lxml (DOM) |
|--------|----------|------------|------------------|---------------|
| 10 MB  | 527 MB/s | 91         | 106              | 5.8x          |
| 100 MB | 575 MB/s | 90         | 101              | 6.4x          |

lxml's `iterparse` streaming mode is its fastest path for large files;
Parshred's full-DOM path still beats it because the DOM build is zero-copy
into `string_view` ranges with no per-node allocation.

## Quick Start

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(parshred
    GIT_REPOSITORY https://github.com/parshred/parshred.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(parshred)
target_link_libraries(myapp PRIVATE parshred::parshred)
```

### Build from Source

```bash
git clone https://github.com/parshred/parshred.git
cd parshred
cmake --preset release
cmake --build build/release
sudo cmake --install build/release
```

### Python

```bash
pip install parshred
```

## Usage Examples

### DOM Parsing (C++)

```cpp
#include <parshred/parshred.hpp>

std::string xml = "<catalog><book id=\"1\">Title</book></catalog>";
auto dom = parshred::fast_dom_parse<0>(xml.data(), xml.size());

// XPath queries
auto title = parshred::xpath::evaluate_string(dom, "/catalog/book/text()");
auto id = parshred::xpath::evaluate_string(dom, "/catalog/book/@id");
```

### Pull Parser (C++)

```cpp
#include <parshred/pull_parser.hpp>

parshred::XmlReader reader(xml);
while (reader.next()) {
    if (reader.is_start_element("book")) {
        auto id = reader.attribute("id");
        auto text = reader.read_element_text();
    }
}
```

### Python

```python
import parshred

doc = parshred.parse_file("catalog.xml")
for book in doc.xpath("//book"):
    print(book.attr("id"), book.text)
```

### Build XML Programmatically

```cpp
#include <parshred/writer.hpp>

parshred::DomBuilder builder;
builder.start_element("catalog");
builder.start_element("book");
builder.add_attribute("id", "1");
builder.add_text("Title");
builder.end_element();
builder.end_element();

auto dom = builder.build();
parshred::XmlWriter writer;
std::string xml = writer.serialize(dom);
```

## Documentation

- [Quick Start Guide](docs/quickstart.md)
- [API Reference](docs/api_reference.md)
- [Performance Guide](docs/performance.md)
- [Conformance](docs/conformance.md)
- [Migration from libxml2/pugixml/RapidXML](docs/migration.md)

## Requirements

- C++20 compiler: GCC 12+, Clang 15+, or MSVC 2022+
- CMake 3.20+
- x86-64 with SSE4.2 minimum (AVX2/AVX-512 recommended) or ARM64 with NEON
- Python 3.9+ (for the optional Python bindings)

## License

Parshred is licensed under the **GNU Affero General Public License v3.0 or later**
([AGPL-3.0-or-later](https://www.gnu.org/licenses/agpl-3.0)) for open-source use.
See [LICENSE](LICENSE) for the full license text.

A commercial license is available for proprietary use. See
[COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md) for details.

## Contributing

Contributions are welcome. Please see [CONTRIBUTING.md](CONTRIBUTING.md) for
guidelines on code style, testing, and submitting pull requests.
