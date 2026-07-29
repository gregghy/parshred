# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.3] - 2026-07-29

### Fixed
- macOS x86_64 wheels: moved from macos-13 (Intel, poor runner availability)
  to macos-14 (ARM) with cross-compilation. All macOS wheels now build on
  macos-14 runners which have abundant availability.

## [0.1.2] - 2026-07-29

### Fixed
- macOS wheel builds: replaced universal2 cross-compilation (which queued
  indefinitely on scarce macos-13 Intel runners) with native arch-native
  wheels — x86_64 on macos-13, arm64 on macos-14. Faster builds and better
  runner availability.

## [0.1.1] - 2026-07-29

### Added
- Official W3C XML Conformance Test Suite runner (`tests/w3c_xmlconf_runner`)
  wired into CI (`.github/workflows/w3c.yml`); JUnit report published as a
  workflow artifact. Reference run: 359 pass / 928 fail (not-wf gap) /
  732 skip out of 2019 cases.
- Benchmark regression gate (`.github/workflows/bench.yml`) with a
  ratcheting baseline; fails on >10% throughput regression. Self-contained
  `bench/bench_regression.cpp` + `tools/check_bench_regression.py`.
- cibuildwheel workflow (`.github/workflows/wheels.yml`) producing
  manylinux2014 (x86-64 + aarch64), macOS universal2, and Windows AMD64
  wheels with PyPI trusted-publishing on tag pushes.
- Contributor License Agreements (Individual + Corporate) under `docs/cla/`
  with CLA Assistant config (`.github/cla.yml`).
- TSan and MSan CI jobs in `ci.yml` (MSan informational pending an
  instrumented libc++ build).
- Expanded fuzz corpus seeds (malformed, entities, deep nesting, many
  attributes, namespaces).

### Changed
- Rewrote README performance section with measured, machine-attributed
  numbers. Honest headline: matches RapidXML within ~5% at 64 KB–16 MB,
  1.5–2.4x faster at 64 MB+. Python: 5–8x faster than lxml.
- Polished `COMMERCIAL_LICENSE.md` with explicit grant scope, pricing
  tiers, evaluation licenses, and escrow terms.
- Python wheel built portable (`-march=x86-64-v2` baseline) instead of
  `-march=native`, so published wheels do not crash on CPUs lacking the
  build host's SIMD features. Runtime SIMD dispatch remains in the core
  library.
- Updated `docs/conformance.md` with the W3C suite results table and
  corrected the outdated XPath coverage table (arithmetic, boolean
  operators, union, string/number functions are all supported per
  `test_xpath_extended`).

### Fixed
- `-Wmissing-field-initializers` warnings in `dom_fast.hpp` and
  `writer.hpp` (document node now fully zero-initialized via designated
  initializers in declaration order).
- Unused-variable warning in `dtd.hpp` (`spec_start`).
- Unused-but-set-variable warning in `test_conformance.cpp`.
- Dangling-else warning in `test_normalization.cpp`.

## [0.1.0] - 2025-07-14

### Added
- SIMD-accelerated XML tokenizer (SSE4.2, AVX2, AVX-512, ARM NEON)
- SAX parser with zero-copy string_view callbacks
- Fused single-pass SAX parser (FastSAX) with Turbo mode
- DOM parser with two modes: 80-byte full nodes, 32-byte compact nodes
- DOM parser matching RapidXML at small/mid sizes and 1.5–2.4x faster at 64 MB+
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
- Matches RapidXML at 64 KB–16 MB; 1.5–2.4x faster at 64 MB+
- Python: 5–8x faster than lxml
