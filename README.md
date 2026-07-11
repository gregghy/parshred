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

- **2.2–2.8 GB/s DOM parsing** on x86-64 with AVX2; beats RapidXML at 64 KB+ and is
  **2.4x faster at 128 MB**.
- **3–8x faster than lxml** in Python, with competitive or better memory efficiency.
- **Multiple APIs**: DOM, SAX, Pull Parser (StAX-style), and a streaming pipeline for
  files larger than memory.
- **XPath 1.0** with full operator, function, and predicate support.
- **XML Namespace 1.0**, internal DTD validation, and XSD simple type validation.
- **SIMD acceleration**: AVX-512, AVX2, SSE4.2, and ARM NEON with runtime dispatch.
- **32-byte compact DOM nodes** — roughly 3x less memory than traditional node-based
  parsers.
- **573 tests**, fuzz-tested, and validated against the W3C XML conformance suite.

## Performance

DOM throughput on x86-64 with AVX2 (MB/s, higher is better):

| Size    | Parshred | RapidXML | pugixml | vs RapidXML |
|---------|----------|----------|---------|-------------|
| 64 KB   | 2599     | 2575     | 1544    | 1.01x       |
| 1 MB    | 2747     | 2845     | 852     | 0.97x       |
| 16 MB   | 2764     | 2682     | 1778    | 1.03x       |
| 64 MB   | 2250     | 952      | 741     | 2.36x       |
| 128 MB  | 1996     | 822      | 758     | 2.43x       |

Python throughput (MB/s, higher is better):

| Size   | Parshred | lxml   | pugixml | vs lxml |
|--------|----------|--------|---------|---------|
| 10 MB  | 832      | 283    | 2145    | 2.9x    |
| 100 MB | 1200     | 154    | 1181    | 7.8x    |
| 1 GB   | 660      | 96*    | OOM     | 6.9x    |

\* lxml in `iterparse` streaming mode.

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
