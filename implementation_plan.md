# Parshred Phase 1 — World-Class CPU XML Parser

Build the foundation described in the project spec's "First 30-Day Milestone": mmap input, SIMD structural scanner, SAX interface, Python bindings, and benchmark suite. The system you have is ideal — AMD Ryzen 7640U with AVX-512, GCC 16, CMake 4.3, GTest 1.17, plus libxml2/pugixml/expat already installed for benchmarking.

---

## Proposed Changes

### Project Structure

```
parshred/
├── CMakeLists.txt              # Root CMake (C++20, FetchContent for deps)
├── CMakePresets.json            # Debug/Release/Benchmark presets
├── pyproject.toml               # Python packaging (scikit-build-core + pybind11)
├── LICENSE                      # MIT
├── README.md
├── include/parshred/
│   ├── parshred.hpp             # Umbrella public header
│   ├── common.hpp               # Types, error codes, string_view utilities
│   ├── mmap_reader.hpp          # Memory-mapped file I/O
│   ├── simd_scanner.hpp         # SIMD structural character scanner
│   ├── tokenizer.hpp            # Token stream from structural index
│   ├── sax_parser.hpp           # SAX streaming parser
│   └── arena.hpp                # Arena/pool allocator
├── src/
│   ├── CMakeLists.txt
│   ├── mmap_reader.cpp
│   ├── simd_scanner.cpp         # Runtime dispatch: AVX-512 → AVX2 → SSE4.2 → scalar
│   ├── simd/
│   │   ├── scanner_avx512.cpp
│   │   ├── scanner_avx2.cpp
│   │   ├── scanner_sse42.cpp
│   │   └── scanner_scalar.cpp
│   ├── tokenizer.cpp
│   ├── sax_parser.cpp
│   └── arena.cpp
├── python/
│   ├── CMakeLists.txt
│   ├── bindings.cpp             # pybind11 module
│   └── parshred/
│       └── __init__.py
├── bench/
│   ├── CMakeLists.txt
│   ├── bench_main.cpp           # Google Benchmark harness
│   ├── bench_scanner.cpp
│   ├── bench_sax.cpp
│   ├── bench_vs_libxml2.cpp
│   ├── bench_vs_pugixml.cpp
│   ├── bench_vs_expat.cpp
│   └── data/                    # Test XML files (generated at build time)
│       └── generate_xml.py
├── tests/
│   ├── CMakeLists.txt
│   ├── test_mmap.cpp
│   ├── test_scanner.cpp
│   ├── test_tokenizer.cpp
│   ├── test_sax.cpp
│   └── test_security.cpp        # XML bomb, deep nesting, malicious entities
└── tools/
    └── parshred_cli.cpp          # CLI: `parshred <file.xml>` — validate / count / time
```

---

### Build System

#### [NEW] CMakeLists.txt (root)
- Project `parshred`, C++20, `CMAKE_CXX_EXTENSIONS OFF`
- Options: `PARSHRED_BUILD_TESTS`, `PARSHRED_BUILD_BENCH`, `PARSHRED_BUILD_PYTHON`, `PARSHRED_BUILD_CLI`
- FetchContent for GoogleTest and Google Benchmark
- pybind11 via `find_package` (installed by scikit-build-core during `pip install`)
- SIMD flags: compile scanner_avx512.cpp with `-mavx512f -mavx512bw`, scanner_avx2.cpp with `-mavx2`, etc.
- Runtime CPUID dispatch — the library works on any x86-64 CPU

#### [NEW] CMakePresets.json
- `debug`: `-DCMAKE_BUILD_TYPE=Debug -DPARSHRED_BUILD_TESTS=ON`
- `release`: `-DCMAKE_BUILD_TYPE=Release -DPARSHRED_BUILD_BENCH=ON -DPARSHRED_BUILD_CLI=ON`

#### [NEW] pyproject.toml
- scikit-build-core backend, pybind11 build dependency

---

### Core Components

#### Memory Engine — `mmap_reader.hpp/.cpp`
- `MmapReader` class: opens file via `mmap(2)`, returns `std::span<const char>`
- `madvise(MADV_SEQUENTIAL)` for large files
- RAII semantics (unmaps on destruction)
- Buffer fallback for stdin/pipe input
- Zero-copy: downstream components operate on the mapped span directly

#### Arena Allocator — `arena.hpp/.cpp`
- Simple bump allocator with configurable block size (default 64 KB)
- Used by tokenizer and SAX parser for temporary string storage
- Thread-local arenas for future parallel parsing (Phase 2)

#### SIMD Structural Scanner — `simd_scanner.hpp`, `src/simd/*.cpp`

This is the performance-critical core, inspired by simdjson's structural indexing.

**What it does**: Scans the entire input in 64-byte (AVX-512) or 32-byte (AVX2) chunks and produces a **structural index** — a compact array of positions where structural characters (`< > / " ' = &`) appear, with quote masking so that characters inside attribute values are skipped.

**AVX-512 implementation** (`scanner_avx512.cpp`):
1. Load 64 bytes into `__m512i`
2. Use `_mm512_cmpeq_epi8_mask` to produce 64-bit masks for each structural char
3. Use XOR-prefix-sum (carry-less multiply via `_mm_clmulepi64_si128`) to track open/close quote state
4. Mask out characters inside quotes
5. Use `_tzcnt_u64` / `_blsr_u64` to iterate set bits → store positions into output array

**AVX2 implementation** (`scanner_avx2.cpp`):
- Same algorithm with `__m256i` (32 bytes), two halves combined into 64-bit masks

**SSE4.2 fallback** (`scanner_sse42.cpp`):
- 16-byte chunks via `_mm_cmpeq_epi8`

**Scalar fallback** (`scanner_scalar.cpp`):
- Simple loop for non-x86 or very old CPUs

**Runtime dispatch** (`simd_scanner.cpp`):
- CPUID detection at load time → function pointers to best available impl
- Public API: `StructuralIndex scan(std::span<const char> input)`
- `StructuralIndex` is a flat `std::vector<uint32_t>` of byte offsets

#### XML Tokenizer — `tokenizer.hpp/.cpp`
Walks the structural index sequentially and produces a token stream:

```cpp
enum class TokenType : uint8_t {
    StartTag,       // <name
    EndTag,         // </name>
    SelfClosingTag, // <name/>
    AttributeName,  // key
    AttributeValue, // "value"
    Text,           // character data between tags
    CData,          // <![CDATA[...]]>
    Comment,        // <!-- ... -->
    ProcessingInstruction, // <?...?>
    EntityRef,      // &amp; etc.
    DocType,        // <!DOCTYPE ...>
};

struct Token {
    TokenType type;
    std::string_view text;  // zero-copy view into mmap'd data
};
```

- Uses `string_view` exclusively — no copies
- Validates well-formedness (mismatched tags, invalid names) with clear error positions
- Entity reference detection (but NOT expansion — that's the parser's job)

#### SAX Parser — `sax_parser.hpp/.cpp`
Event-driven streaming parser built on top of the tokenizer:

```cpp
class SaxParser {
public:
    // Callback registration
    using ElementCallback = std::function<void(std::string_view name, const Attributes& attrs)>;
    using TextCallback = std::function<void(std::string_view text)>;

    void on_start_element(ElementCallback cb);
    void on_end_element(std::function<void(std::string_view)> cb);
    void on_text(TextCallback cb);
    void on_cdata(TextCallback cb);
    void on_comment(TextCallback cb);

    // Parsing
    void parse(std::span<const char> input);
    void parse_file(const std::string& path);

    // Security limits
    void set_max_depth(size_t depth);        // default 512
    void set_max_entity_expansions(size_t n); // default 10000
};
```

- Attributes stored as flat array of `{name_view, value_view}` pairs — no map overhead
- Built-in entity expansion for the 5 predefined XML entities (`&lt; &gt; &amp; &apos; &quot;`)
- Security: depth limit, entity expansion limit, input size limit

---

### Python Bindings — `python/bindings.cpp`

```python
import parshred

# SAX-style
def on_start(name, attrs):
    print(f"<{name}>", dict(attrs))

parser = parshred.SaxParser()
parser.on_start_element(on_start)
parser.parse_file("large.xml")

# Or load+iterate convenience
for event in parshred.iterparse("large.xml"):
    print(event)
```

- `SaxParser` class with Pythonic callback registration
- `iterparse()` generator yielding `(event_type, name, attrs/text)` tuples
- `parshred.load()` as a simple convenience (returns list of events)

---

### Benchmark Suite — `bench/`

Using Google Benchmark, compare against the three parsers already installed:

| Benchmark | What it measures |
|---|---|
| `bench_scanner` | Raw SIMD scanning throughput (GB/s) |
| `bench_sax` | Full SAX parse throughput |
| `bench_vs_libxml2` | SAX parse vs libxml2's SAX |
| `bench_vs_pugixml` | SAX parse vs pugixml's DOM parse |
| `bench_vs_expat` | SAX parse vs Expat's SAX |

Test data generated by `generate_xml.py`:
- `small.xml` — 1 KB, simple
- `medium.xml` — 1 MB, mixed content
- `large.xml` — 100 MB, repeated records
- `deep.xml` — deeply nested (1000 levels)
- `attrs.xml` — attribute-heavy (50 attrs per element)
- `text.xml` — text-heavy (large CDATA sections)

---

### Tests — `tests/`

| Test file | Coverage |
|---|---|
| `test_mmap` | File mapping, empty file, large file, pipe fallback |
| `test_scanner` | Structural char detection, quote masking, edge cases |
| `test_tokenizer` | All token types, malformed XML, encoding |
| `test_sax` | Callback firing, attributes, nested elements, namespaces |
| `test_security` | XML bomb (billion laughs), max depth, entity limits |

---

### CLI Tool — `tools/parshred_cli.cpp`

Simple command-line utility:
```bash
parshred --validate file.xml    # validate well-formedness
parshred --count file.xml       # count elements/attributes/text nodes
parshred --bench file.xml       # time parsing, report GB/s
```

---

## User Review Required

> [!IMPORTANT]
> **Scope**: This plan implements Phase 1 only (CPU parser). GPU acceleration (Phase 3), parallel chunking (Phase 2), DOM parser, lazy parsing, Arrow conversion, and compression support are deferred to future phases.

> [!IMPORTANT]
> **Python bindings**: pybind11 requires `pip install pybind11 scikit-build-core`. I'll install these before building. OK?

> [!IMPORTANT]
> **Google Benchmark**: Not currently installed. I'll use CMake FetchContent to download it automatically at configure time.

## Open Questions

1. **Namespace support**: Full XML namespace handling (prefix resolution, default namespace inheritance) adds significant complexity. Should Phase 1 include it, or just pass namespace prefixes through as part of the tag name?

2. **UTF-8 only or also UTF-16?**: The spec mentions both. Phase 1 could focus on UTF-8 (95%+ of real-world XML) and add UTF-16 in Phase 2. Sound right?

3. **XML 1.1**: Very rarely used. OK to target XML 1.0 only for Phase 1?

---

## Verification Plan

### Automated Tests
```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

### Benchmarks
```bash
cmake --preset release
cmake --build build/release
./build/release/bench/parshred_bench --benchmark_format=console
```

### Python Bindings
```bash
pip install -e .
python -c "import parshred; print(parshred.parse_file('bench/data/small.xml'))"
```

### Manual Verification
- Parse a real-world large XML file (e.g., Wikipedia dump sample) and compare output against libxml2
- Run the CLI tool on various XML files and verify output correctness
