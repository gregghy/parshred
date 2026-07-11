# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-07-14

### Added
- SIMD-accelerated XML tokenizer (SSE4.2, AVX2, AVX-512, ARM NEON)
- SAX parser with zero-copy string_view callbacks
- Fused single-pass SAX parser (FastSAX) with Turbo mode
- DOM parser with two modes: 80-byte full nodes, 32-byte compact nodes
- DOM parser matching/beating RapidXML performance (2.4x faster at 128MB)
- Pull parser (StAX/xmlReader-style API)
- XPath 1.0 engine with arithmetic, comparison, boolean operators, string/number functions
- XML Namespace 1.0 support
- DTD parsing and validation (internal subset)
- XSD simple type validation (30+ built-in types, facets)
- XML writer/serializer with pretty-print
- DOM builder for programmatic XML construction
- Tree modification API (remove_child, remove_attribute)
- Character encoding detection (UTF-8, UTF-16, ISO-8859-1) and transcoding
- Line ending normalization (XML 1.0 section 2.11)
- UTF-8 validation
- Structured error reporting with line/column tracking
- Entity expansion and attribute value normalization (opt-in via FDOM_NORMALIZE)
- Memory-mapped file I/O with automatic fallback
- Chunked streaming pipeline for 10GB+ files
- Platform abstraction layer (Windows/Linux/macOS)
- Python bindings via pybind11
- Security: entity expansion limits, max depth, billion laughs protection
- GitHub Actions CI (Linux/macOS/Windows, GCC/Clang/MSVC)
- Fuzz testing harness (libFuzzer)
- W3C conformance test suite (64 tests)
- Comprehensive documentation (quickstart, API reference, performance guide, migration guides)
- 573 tests passing

### Performance
- SAX: 700-850 MB/s
- DOM (32-byte nodes): 2200-2800 MB/s on x86-64 with AVX2
- Beats RapidXML at 64KB+ files, 2.4x faster at 128MB
- Python: 3-8x faster than lxml, matches pugixml at 100MB+, 660 MB/s at 1GB
