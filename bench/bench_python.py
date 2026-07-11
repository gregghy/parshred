#!/usr/bin/env python3
"""
Benchmark: parshred vs lxml vs pugixml (Python bindings)
Sizes: 10MB, 100MB, 1GB, 10GB

Each parser is tested independently with forced GC between runs
to avoid OOM on memory-constrained systems.
"""

import os
import sys
import time
import tempfile
import gc
import resource

# Add parshred build path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build-python', 'python'))

import parshred
import lxml.etree as lxml_etree
import pugixml

# ── XML Generation ────────────────────────────────────────────────────

def generate_xml_file(path, target_bytes):
    """Generate a realistic XML file of approximately target_bytes size."""
    item_template = '<item id="{:06d}" category="books" active="true">Sample text content here for benchmarking purposes number {}</item>\n'
    item_size = len(item_template.format(0, 0))
    
    header = '<?xml version="1.0" encoding="UTF-8"?>\n<catalog>\n'
    footer = '</catalog>\n'
    overhead = len(header) + len(footer)
    
    num_items = (target_bytes - overhead) // item_size
    
    with open(path, 'w', buffering=8*1024*1024) as f:
        f.write(header)
        for i in range(num_items):
            f.write(item_template.format(i, i))
        f.write(footer)
    
    actual_size = os.path.getsize(path)
    return actual_size

# ── Benchmark Functions ───────────────────────────────────────────────

def bench_parshred(path):
    """Benchmark parshred DOM parsing from file."""
    gc.collect()
    start = time.perf_counter()
    doc = parshred.parse_file(path)
    _ = doc.root.name
    elapsed = time.perf_counter() - start
    count = doc.element_count
    del doc
    gc.collect()
    return elapsed, count

def bench_lxml(path):
    """Benchmark lxml DOM parsing from file."""
    gc.collect()
    start = time.perf_counter()
    tree = lxml_etree.parse(path)
    _ = tree.getroot().tag
    elapsed = time.perf_counter() - start
    del tree
    gc.collect()
    return elapsed

def bench_lxml_iterparse(path):
    """Benchmark lxml iterparse (SAX-like streaming, constant memory)."""
    gc.collect()
    count = 0
    start = time.perf_counter()
    for event, elem in lxml_etree.iterparse(path, events=('end',)):
        if elem.tag == 'item':
            count += 1
            elem.clear()
    elapsed = time.perf_counter() - start
    gc.collect()
    return elapsed, count

def bench_pugixml(path):
    """Benchmark pugixml DOM parsing from file."""
    gc.collect()
    start = time.perf_counter()
    doc = pugixml.pugi.XMLDocument()
    result = doc.load_file(path)
    if not result:
        raise RuntimeError(f"pugixml failed: {result.description()}")
    _ = doc.document_element().name()
    elapsed = time.perf_counter() - start
    del doc
    gc.collect()
    return elapsed

# ── Main ──────────────────────────────────────────────────────────────

def format_size(nbytes):
    if nbytes >= 1_000_000_000:
        return f"{nbytes / 1_000_000_000:.1f} GB"
    elif nbytes >= 1_000_000:
        return f"{nbytes / 1_000_000:.0f} MB"
    else:
        return f"{nbytes / 1_000:.0f} KB"

def format_throughput(nbytes, elapsed):
    if elapsed <= 0:
        return "inf"
    mb_per_sec = (nbytes / 1_000_000) / elapsed
    if mb_per_sec >= 1000:
        return f"{mb_per_sec:.0f} MB/s"
    return f"{mb_per_sec:.0f} MB/s"

def run_single_bench(name, func, path, iterations):
    """Run a single parser benchmark, return best time."""
    print(f"  {name}:", end=" ", flush=True)
    times = []
    for i in range(iterations):
        try:
            result = func(path)
            t = result[0] if isinstance(result, tuple) else result
            times.append(t)
            print(f"{t:.3f}s", end=" ", flush=True)
        except MemoryError:
            print("OOM", end=" ")
            gc.collect()
            break
        except Exception as e:
            print(f"ERR({e})", end=" ")
            break
    if times:
        best = min(times)
        print(f"→ best: {best:.3f}s", flush=True)
        return best
    else:
        print("→ FAILED", flush=True)
        return None

def main():
    print("╔══════════════════════════════════════════════════════════════════════╗")
    print("║   Python XML Parser Benchmark: parshred vs lxml vs pugixml         ║")
    print("╚══════════════════════════════════════════════════════════════════════╝")
    print()
    print(f"  Python: {sys.version.split()[0]}")
    print(f"  parshred: C++ SIMD DOM parser")
    print(f"  lxml: {lxml_etree.__version__} (libxml2)")
    print(f"  pugixml: Python bindings")
    
    # Check RAM
    try:
        with open('/proc/meminfo') as f:
            for line in f:
                if line.startswith('MemAvailable'):
                    mem_gb = int(line.split()[1]) / (1024**2)
                    print(f"  Available RAM: {mem_gb:.1f} GB")
                    break
    except:
        pass
    
    sizes = [
        ("10MB",   10_000_000,     3),
        ("100MB",  100_000_000,    3),
        ("1GB",    1_000_000_000,  2),
        ("10GB",   10_000_000_000, 1),
    ]
    
    all_results = []
    
    with tempfile.TemporaryDirectory(prefix='parshred_bench_', dir='/tmp') as tmpdir:
        for size_name, target_bytes, iterations in sizes:
            # Check disk space
            stat = os.statvfs('/tmp')
            free_gb = (stat.f_bavail * stat.f_frsize) / (1024**3)
            needed_gb = target_bytes / (1024**3) * 1.2
            
            if free_gb < needed_gb:
                print(f"\n  SKIPPING {size_name}: need {needed_gb:.1f} GB disk, have {free_gb:.1f} GB")
                continue
            
            path = os.path.join(tmpdir, f"bench_{size_name}.xml")
            
            print(f"\n{'='*70}")
            print(f"  Generating {size_name} XML file...")
            actual_size = generate_xml_file(path, target_bytes)
            print(f"  Actual size: {format_size(actual_size)}")
            print(f"{'='*70}")
            
            results = {'size': actual_size}
            
            # For very large files (>= 1GB), only use parshred and lxml iterparse
            # (pugixml and lxml DOM need too much RAM for the DOM tree)
            large_file = actual_size >= 800_000_000
            
            # parshred (always try - it's the most memory-efficient DOM)
            t = run_single_bench("parshred", bench_parshred, path, iterations)
            if t: results['parshred'] = t
            
            if not large_file:
                # lxml DOM
                t = run_single_bench("lxml (DOM)", bench_lxml, path, iterations)
                if t: results['lxml'] = t
                
                # pugixml DOM
                t = run_single_bench("pugixml", bench_pugixml, path, iterations)
                if t: results['pugixml'] = t
            
            # lxml iterparse (streaming — works at any size)
            t = run_single_bench("lxml (iterparse)", bench_lxml_iterparse, path, iterations)
            if t: results['lxml_iter'] = t
            
            # Summary
            print(f"\n  {'─'*50}")
            print(f"  Summary for {size_name} ({format_size(actual_size)}):")
            if 'parshred' in results:
                print(f"    parshred:        {format_throughput(actual_size, results['parshred'])}")
            if 'lxml' in results:
                print(f"    lxml (DOM):      {format_throughput(actual_size, results['lxml'])}")
            if 'lxml_iter' in results:
                print(f"    lxml (iterparse):{format_throughput(actual_size, results['lxml_iter'])}")
            if 'pugixml' in results:
                print(f"    pugixml:         {format_throughput(actual_size, results['pugixml'])}")
            
            if 'parshred' in results and 'lxml' in results:
                print(f"    → parshred is {results['lxml']/results['parshred']:.1f}x faster than lxml DOM")
            if 'parshred' in results and 'pugixml' in results:
                print(f"    → parshred is {results['pugixml']/results['parshred']:.1f}x faster than pugixml")
            if 'parshred' in results and 'lxml_iter' in results:
                print(f"    → parshred DOM is {results['lxml_iter']/results['parshred']:.1f}x faster than lxml streaming")
            
            all_results.append((size_name, actual_size, results))
            
            # Delete large files immediately
            if actual_size > 200_000_000:
                os.unlink(path)
                gc.collect()
    
    # Final summary table
    print(f"\n\n{'='*70}")
    print("  FINAL RESULTS — Throughput (MB/s), higher is better")
    print(f"{'='*70}")
    print(f"  {'Size':<8} {'parshred':<14} {'lxml DOM':<14} {'lxml iter':<14} {'pugixml':<14}")
    print(f"  {'─'*8} {'─'*14} {'─'*14} {'─'*14} {'─'*14}")
    
    for size_name, actual_size, results in all_results:
        p = format_throughput(actual_size, results['parshred']) if 'parshred' in results else "—"
        l = format_throughput(actual_size, results['lxml']) if 'lxml' in results else "—"
        li = format_throughput(actual_size, results['lxml_iter']) if 'lxml_iter' in results else "—"
        g = format_throughput(actual_size, results['pugixml']) if 'pugixml' in results else "—"
        print(f"  {size_name:<8} {p:<14} {l:<14} {li:<14} {g:<14}")
    
    print()
    print("  Notes:")
    print("  - parshred: full DOM parse (SIMD-accelerated, 32-byte nodes)")
    print("  - lxml DOM: full tree construction via libxml2")
    print("  - lxml iter: SAX-like streaming (constant memory)")
    print("  - pugixml: DOM parse via C++ bindings")
    print("  - For >=1GB files, only parshred DOM and lxml iterparse are tested")
    print("    (other parsers would OOM on this system)")
    print()

if __name__ == "__main__":
    main()
