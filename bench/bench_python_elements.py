#!/usr/bin/env python3
"""
Benchmark: parshred vs lxml vs pugixml
Element counts: 100, 1000, 5000, 10000

Each test generates a realistic XML document with the specified number of
elements, then measures DOM parse time across many iterations.
Small files are dominated by call overhead, so we use high iteration counts
to get stable timings.
"""

import os
import sys
import time
import gc
import tempfile
import statistics

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build-python-opt', 'python'))

import parshred
import lxml.etree as lxml_etree
import pugixml

# ── XML Generation ────────────────────────────────────────────────────

def generate_xml(num_elements):
    """Generate a realistic XML string with the given number of elements."""
    parts = ['<?xml version="1.0" encoding="UTF-8"?>\n<catalog>\n']
    for i in range(num_elements):
        parts.append(
            f'  <item id="{i}" category="books" price="{19.99 + i * 0.01:.2f}" '
            f'in_stock="true" rating="{(i % 5) + 1}">\n'
            f'    <title>Book Title Number {i}</title>\n'
            f'    <author>Author {i % 50}</author>\n'
            f'    <year>{2000 + i % 25}</year>\n'
            f'    <description>This is the description for item {i}. '
            f'It contains enough text to be realistic.</description>\n'
            f'  </item>\n'
        )
    parts.append('</catalog>\n')
    return ''.join(parts)


# ── Benchmark Runners ─────────────────────────────────────────────────

def bench_parshred_str(xml_str, iterations):
    """Benchmark parshred parsing from string."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        doc = parshred.parse(xml_str)
        _ = doc.root.name
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del doc
    return times


def bench_parshred_file(path, iterations):
    """Benchmark parshred parsing from file."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        doc = parshred.parse_file(path)
        _ = doc.root.name
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del doc
    return times


def bench_lxml_str(xml_bytes, iterations):
    """Benchmark lxml parsing from bytes."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        root = lxml_etree.fromstring(xml_bytes)
        _ = root.tag
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del root
    return times


def bench_lxml_file(path, iterations):
    """Benchmark lxml parsing from file."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        tree = lxml_etree.parse(path)
        _ = tree.getroot().tag
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del tree
    return times


def bench_pugixml_str(xml_str, iterations):
    """Benchmark pugixml parsing from string."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        doc = pugixml.pugi.XMLDocument()
        result = doc.load_string(xml_str)
        if not result:
            raise RuntimeError(f"pugixml parse failed: {result.description()}")
        _ = doc.document_element().name()
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del doc
    return times


def bench_pugixml_file(path, iterations):
    """Benchmark pugixml parsing from file."""
    times = []
    for _ in range(iterations):
        gc.collect()
        gc.disable()
        start = time.perf_counter()
        doc = pugixml.pugi.XMLDocument()
        result = doc.load_file(path)
        if not result:
            raise RuntimeError(f"pugixml parse failed: {result.description()}")
        _ = doc.document_element().name()
        elapsed = time.perf_counter() - start
        gc.enable()
        times.append(elapsed)
        del doc
    return times


# ── Reporting ─────────────────────────────────────────────────────────

def format_us(seconds):
    """Format seconds as microseconds."""
    return f"{seconds * 1e6:.0f} us"


def format_ms(seconds):
    """Format seconds as milliseconds."""
    return f"{seconds * 1e3:.2f} ms"


def format_time(seconds):
    if seconds < 0.001:
        return format_us(seconds)
    return format_ms(seconds)


def format_throughput(nbytes, seconds):
    if seconds <= 0:
        return "inf"
    mb = nbytes / 1e6
    return f"{mb / seconds:.0f} MB/s"


def run_benchmark_set(num_elements, tmpdir):
    """Run all benchmarks for a given element count."""
    # Generate XML
    xml_str = generate_xml(num_elements)
    xml_bytes = xml_str.encode('utf-8')
    file_size = len(xml_bytes)

    # Write to file
    path = os.path.join(tmpdir, f'bench_{num_elements}.xml')
    with open(path, 'wb') as f:
        f.write(xml_bytes)

    # Iteration count — more iterations for small files
    if num_elements <= 100:
        iterations = 500
    elif num_elements <= 1000:
        iterations = 200
    elif num_elements <= 5000:
        iterations = 50
    else:
        iterations = 30

    print(f"\n{'='*74}")
    print(f"  {num_elements} elements | {file_size:,} bytes ({file_size/1024:.1f} KB) | {iterations} iterations")
    print(f"{'='*74}")

    results = {}

    # -- String parsing --
    print(f"\n  --- In-Memory (string) Parsing ---")

    print(f"  parshred :", end=" ", flush=True)
    times = bench_parshred_str(xml_str, iterations)
    best = min(times)
    med = statistics.median(times)
    results['parshred_str'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    print(f"  lxml     :", end=" ", flush=True)
    times = bench_lxml_str(xml_bytes, iterations)
    best = min(times)
    med = statistics.median(times)
    results['lxml_str'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    print(f"  pugixml  :", end=" ", flush=True)
    times = bench_pugixml_str(xml_str, iterations)
    best = min(times)
    med = statistics.median(times)
    results['pugixml_str'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    # -- File parsing --
    print(f"\n  --- File Parsing ---")

    print(f"  parshred :", end=" ", flush=True)
    times = bench_parshred_file(path, iterations)
    best = min(times)
    med = statistics.median(times)
    results['parshred_file'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    print(f"  lxml     :", end=" ", flush=True)
    times = bench_lxml_file(path, iterations)
    best = min(times)
    med = statistics.median(times)
    results['lxml_file'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    print(f"  pugixml  :", end=" ", flush=True)
    times = bench_pugixml_file(path, iterations)
    best = min(times)
    med = statistics.median(times)
    results['pugixml_file'] = best
    print(f"best={format_time(best):>10}  median={format_time(med):>10}  {format_throughput(file_size, best)}")

    # -- Summary --
    print(f"\n  Ratios (string parse, best times):")
    if results['lxml_str'] > 0:
        r = results['lxml_str'] / results['parshred_str']
        label = "faster" if r > 1 else "slower"
        print(f"    parshred vs lxml:    {r:.2f}x {label}")
    if results['pugixml_str'] > 0:
        r = results['pugixml_str'] / results['parshred_str']
        label = "faster" if r > 1 else "slower"
        print(f"    parshred vs pugixml: {r:.2f}x {label}")

    return num_elements, file_size, results


def main():
    print("=" * 74)
    print("  Python XML Parser Benchmark: parshred vs lxml vs pugixml")
    print("  Element counts: 100, 1000, 5000, 10000")
    print("=" * 74)
    print()
    print(f"  Python:   {sys.version.split()[0]}")
    print(f"  parshred: SIMD-accelerated C++20 DOM (AGPL-3.0)")
    print(f"  lxml:     {lxml_etree.__version__} (libxml2 C bindings)")
    print(f"  pugixml:  C++ DOM via Python bindings")

    element_counts = [100, 1000, 5000, 10000]
    all_results = []

    with tempfile.TemporaryDirectory(prefix='parshred_bench_') as tmpdir:
        for n in element_counts:
            result = run_benchmark_set(n, tmpdir)
            all_results.append(result)

    # Final summary table
    print(f"\n\n{'='*74}")
    print("  SUMMARY — String Parse Throughput (MB/s, higher is better)")
    print(f"{'='*74}")
    print(f"  {'Elements':<10} {'Size':<10} {'parshred':<12} {'lxml':<12} {'pugixml':<12} {'vs lxml':<10} {'vs pugi':<10}")
    print(f"  {'--------':<10} {'--------':<10} {'--------':<12} {'--------':<12} {'--------':<12} {'--------':<10} {'--------':<10}")

    for num_elements, file_size, results in all_results:
        def tp(key):
            if key in results and results[key] > 0:
                return f"{file_size / 1e6 / results[key]:.0f}"
            return "N/A"

        vs_l = f"{results['lxml_str']/results['parshred_str']:.2f}x" if results.get('parshred_str') and results.get('lxml_str') else "N/A"
        vs_p = f"{results['pugixml_str']/results['parshred_str']:.2f}x" if results.get('parshred_str') and results.get('pugixml_str') else "N/A"

        size_str = f"{file_size/1024:.0f} KB" if file_size < 1024*1024 else f"{file_size/1e6:.1f} MB"

        print(f"  {num_elements:<10} {size_str:<10} {tp('parshred_str'):<12} {tp('lxml_str'):<12} {tp('pugixml_str'):<12} {vs_l:<10} {vs_p:<10}")

    print()
    print(f"  {'='*74}")
    print("  SUMMARY — File Parse Throughput (MB/s, higher is better)")
    print(f"  {'='*74}")
    print(f"  {'Elements':<10} {'Size':<10} {'parshred':<12} {'lxml':<12} {'pugixml':<12} {'vs lxml':<10} {'vs pugi':<10}")
    print(f"  {'--------':<10} {'--------':<10} {'--------':<12} {'--------':<12} {'--------':<12} {'--------':<10} {'--------':<10}")

    for num_elements, file_size, results in all_results:
        def tp(key):
            if key in results and results[key] > 0:
                return f"{file_size / 1e6 / results[key]:.0f}"
            return "N/A"

        vs_l = f"{results['lxml_file']/results['parshred_file']:.2f}x" if results.get('parshred_file') and results.get('lxml_file') else "N/A"
        vs_p = f"{results['pugixml_file']/results['parshred_file']:.2f}x" if results.get('parshred_file') and results.get('pugixml_file') else "N/A"

        size_str = f"{file_size/1024:.0f} KB" if file_size < 1024*1024 else f"{file_size/1e6:.1f} MB"

        print(f"  {num_elements:<10} {size_str:<10} {tp('parshred_file'):<12} {tp('lxml_file'):<12} {tp('pugixml_file'):<12} {vs_l:<10} {vs_p:<10}")

    print()


if __name__ == "__main__":
    main()
