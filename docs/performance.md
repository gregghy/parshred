# Parshred Performance Guide

---

## Parser Selection

Parshred provides multiple parsing APIs targeting different trade-offs between speed,
memory usage, and feature completeness. The decision tree below covers the common cases.

```
Do you need a DOM tree?
  Yes -> Is file smaller than ~64 MB?
           Yes -> fast_dom_parse<FDOM_FASTEST>() for speed
                  fast_dom_parse<0>() for full features
                  dom_parse<DOM_FASTEST>() for RapidXML-style in-situ
           No  -> ChunkedParser or ParallelParser (streaming)
  No  -> Do you need entity expansion?
           No  -> fast_parse_turbo() -- maximum throughput
           Yes -> FastSaxParser<Normal> or SaxParser
         Is the file > 1 GB?
           Yes -> ChunkedParser<Turbo> (constant memory)
```

### FastDom (`fast_dom_parse`)

Best for: structure extraction, XPath queries, cases where you need a tree but not
a modifiable DOM. Nodes are 32 bytes; two nodes fit per 64-byte cache line. The
entire tree is one contiguous `malloc` allocation — no per-node heap overhead.

Flags control what is stored:

| Flag | Effect |
|---|---|
| `FDOM_FASTEST` | Skip text nodes and comments. Fastest possible. |
| `FDOM_NO_ATTRS` | Skip attributes. |
| `FDOM_NO_TEXT` | Skip text nodes. |
| `FDOM_NO_COMMENT` | Skip comments. |
| `FDOM_NORMALIZE` | Expand entities and normalize attribute whitespace. |
| `0` (default) | Build full tree including text nodes and attributes. |

When you only need to count elements or extract a few attribute values, use
`FDOM_FASTEST`. The parser skips scanning and storing text content entirely,
which reduces both parse time and memory by 30–50% on text-heavy documents.

### DOM Parser (`dom_parse`)

Best for: applications already structured around tree mutation, or that need the
`XmlDocument` range-for API. Nodes are 80 bytes (parent pointer included).

`DOM_FASTEST` with a mutable buffer enables in-situ parsing: the parser writes
null bytes directly into the input to terminate strings, eliminating all string
copies. This is the exact technique used by RapidXML.

### FastSaxParser / fast_parse_turbo

Best for: processing large files where you need to visit every element but do not
need the entire tree in memory simultaneously. This is the highest-throughput path
for files of any size.

`ParseMode::Turbo`:
- No entity expansion
- No depth checking
- No statistics collection
- No branch for security limits

`ParseMode::Normal`:
- Entity expansion (`&lt;`, `&amp;`, `&#x...;`, etc.)
- Depth limit checked on every start-element
- Statistics accumulated in `Stats` struct

### ChunkedParser

Best for: files larger than available RAM, or when memory budget is fixed. Processes
the file in 2 MB chunks (configurable). Maintains minimal state across chunk
boundaries — a small boundary buffer for tags that straddle a chunk edge.

Default chunk size is 2 MB. The overlap region is 64 KB, which covers any
realistically-sized individual XML tag. Reduce `chunk_size` to lower peak memory;
increase it to improve throughput by reducing boundary-handling overhead.

### ParallelParser

Best for: files where disk I/O is the bottleneck. Uses `madvise(MADV_WILLNEED)` to
prefetch chunk N+2 while chunk N is being parsed. The number of worker threads
defaults to `std::thread::hardware_concurrency()`. Since SAX events must be emitted
in document order, only I/O prefetch is parallelised — the parse itself is serial.

---

## File Size Recommendations

| File size | Recommended approach |
|---|---|
| < 64 KB | `fast_dom_parse<FDOM_FASTEST>()` |
| 64 KB – 64 MB | `fast_dom_parse<FDOM_FASTEST>()` or `fast_parse_turbo()` |
| 64 MB – 1 GB | `fast_parse_turbo()` with `SaxHandler` |
| > 1 GB | `ChunkedParser<Turbo>` or `ParallelParser<Turbo>` |

The crossover between DOM and SAX for raw throughput is typically around 4–8 MB.
Below this threshold, DOM parse time is dominated by tree construction, which
amortizes well when traversal follows immediately. Above this threshold, SAX
avoids the memory cost of materializing a large tree.

---

## Compiler Flags

The library is compiled with architecture-specific SIMD flags per translation unit.
The scanner object files are compiled with the minimum ISA required:

| Object | Compile flags | Required CPU |
|---|---|---|
| `scanner_scalar.cpp` | (none extra) | Any x86-64 |
| `scanner_sse42.cpp` | `-msse4.2 -mpclmul` | Intel Nehalem 2008+ |
| `scanner_avx2.cpp` | `-mavx2 -mpclmul` | Intel Haswell 2013+ |
| `scanner_avx512.cpp` | `-mavx512f -mavx512bw -mpclmul` | Skylake-X 2017+ / Zen 4 2022+ |
| `fast_sax.cpp` | `-mavx2` | Haswell 2013+ |

Runtime dispatch (via CPUID) selects the best available scanner at program start.
No special flags are required in your application's CMakeLists.txt; link against
`libparshred.a` and the correct backend is used automatically.

For your own application code that calls parshred APIs, the important flags are:

```bash
# Minimum viable (SSE 4.2 baseline)
-O3 -std=c++20

# Recommended for development machines (Haswell+)
-O3 -std=c++20 -mavx2 -march=native

# Maximum for known AVX-512 hardware
-O3 -std=c++20 -mavx512f -mavx512bw -march=native
```

`-march=native` enables all ISA extensions available on the build host and also
enables auto-vectorisation of your handler code. It produces binaries that may not
run on older CPUs.

Link-time optimisation (LTO) can further improve performance when the handler is
in a separate translation unit:

```cmake
target_compile_options(my_target PRIVATE -flto=auto)
target_link_options(my_target PRIVATE -flto=auto)
```

---

## Memory Usage Characteristics

### `FastDom`

- Node array: `node_count * 32` bytes (one `malloc` call, no per-node overhead)
- Values buffer: roughly proportional to total text content length
- Initial estimate: one node per 35 bytes of input; realloc'd if exceeded
- `FDOM_FASTEST` + `FDOM_NO_ATTRS`: values buffer not allocated at all

Attribute values in the default parse mode are zero-copy: `value_offset` is a byte
offset into the original input buffer, so attribute values cost no additional memory
beyond the `FastNode` record itself.

Text node values are stored in the `values` vector (a separate copy from input),
because text content may be fragmented across the input and entity-expanded values
cannot point into the original buffer.

### `XmlDocument` / `dom_parse`

- Nodes: pool-allocated in 64 KB blocks (`NodePool`)
- Node size: 80 bytes each (includes parent pointer)
- String pool: arena-allocated in 64 KB blocks for entity-expanded values
- In-situ mode (`DOM_INSITU`): no string pool; names/values point into the
  (mutated) input buffer

### `SaxParser` / `FastSaxParser`

- Zero tree allocation — all values are `string_view` into the input buffer
- Attribute scratch buffer: 64-element inline array in `FastSaxParser` for
  the common case; falls back to a `std::vector` for elements with >64 attributes
- Entity expansion buffer: one `std::string` reused across calls (Normal mode only)

### `ChunkedParser`

- Peak memory: `chunk_size + overlap` bytes of XML data in flight
- One `FastSaxParser` instance (no per-chunk allocation beyond the above)
- Boundary buffer: at most `overlap` bytes (64 KB default)

---

## Benchmark Results

Results from `bench/bench_dom.cpp` using a synthetic XML workload
(`<item id="N" name="valueN" status="active">text content</item>` records
inside a `<root>` element). Compiled with `-O3 -mavx2`. Numbers are median
throughput across multiple runs.

Hardware: x86-64, AVX2, L3 cache sufficient for files up to 16 MB.

| File size | FastDOM (`FDOM_FASTEST`) | DOM fastest | SAX turbo | RapidXML fastest | pugixml minimal |
|---|---|---|---|---|---|
| 512 B | ~1,800 MB/s | ~1,600 MB/s | ~2,000 MB/s | ~1,400 MB/s | ~900 MB/s |
| 1 KB | ~2,100 MB/s | ~1,900 MB/s | ~2,300 MB/s | ~1,600 MB/s | ~1,050 MB/s |
| 4 KB | ~2,400 MB/s | ~2,100 MB/s | ~2,700 MB/s | ~1,800 MB/s | ~1,200 MB/s |
| 16 KB | ~2,600 MB/s | ~2,300 MB/s | ~2,900 MB/s | ~2,000 MB/s | ~1,300 MB/s |
| 64 KB | ~2,800 MB/s | ~2,500 MB/s | ~3,100 MB/s | ~2,100 MB/s | ~1,350 MB/s |
| 256 KB | ~3,000 MB/s | ~2,700 MB/s | ~3,300 MB/s | ~2,200 MB/s | ~1,400 MB/s |
| 1 MB | ~3,200 MB/s | ~2,800 MB/s | ~3,500 MB/s | ~2,300 MB/s | ~1,450 MB/s |
| 4 MB | ~3,300 MB/s | ~2,900 MB/s | ~3,600 MB/s | ~2,350 MB/s | ~1,450 MB/s |
| 16 MB | ~3,100 MB/s | ~2,700 MB/s | ~3,400 MB/s | ~2,250 MB/s | ~1,400 MB/s |
| 64 MB | ~2,800 MB/s | ~2,500 MB/s | ~3,100 MB/s | ~2,050 MB/s | ~1,300 MB/s |
| 128 MB | ~2,600 MB/s | ~2,300 MB/s | ~2,900 MB/s | ~1,900 MB/s | ~1,200 MB/s |

The throughput drop above 16 MB reflects LLC pressure (file no longer fits in L3).
`ChunkedParser` with `madvise` prefetch partially compensates by overlapping I/O
with parse for very large files.

Parshred is approximately **1.3–1.5x faster than RapidXML** in fast-DOM mode and
**1.4–1.7x faster in turbo SAX mode** on this workload, and **2–2.5x faster than
pugixml minimal**.

These are throughput numbers on a specific synthetic workload. Real-world results
depend on element depth, attribute density, text content ratio, and cache pressure.

---

## Performance Tips

**Use `FDOM_FASTEST` for counting and structure extraction.**
If you only need to count elements or read a few attributes, using `FDOM_FASTEST`
avoids allocating or populating the values buffer. For a 1 MB file this typically
saves 100–200 KB of allocation and 15–20% of parse time.

**Pre-allocate the values buffer.**
If you are parsing many similar documents in a loop, reusing the `FastDom` struct
is not directly supported (build, use, destroy). However, `fast_dom_parse` uses
`len / 35 + 64` as its initial node capacity estimate, which is usually accurate.
If your documents are known to be denser (e.g., many short elements), pass a larger
estimate by wrapping `fast_dom_parse` in a custom loop with pre-allocated storage.

**Reuse a `SaxParser` across calls.**
`SaxParser` registers callbacks once. Calling `parse_string()` or `parse_file()`
multiple times reuses the same callback objects. `reset_stats()` clears counters
between calls if you are accumulating per-file statistics.

**Use `parse_chunk` for incremental feeding.**
`FastSaxParser::parse_chunk()` resumes parsing without resetting depth or statistics.
Use it when data arrives in network fragments and you cannot buffer the entire
document.

**Turbo mode eliminates branch overhead on every character.**
`ParseMode::Turbo` removes all entity expansion code paths from the compiled parser.
With a conformant XML producer (which will never emit `&amp;` in attribute names,
etc.), turbo mode is always correct for reading data that does not use non-predefined
entities.

**Avoid unnecessary `std::string` copies of `string_view` results.**
`dom.name()`, `dom.value()`, and `dom.attr()` return `string_view` objects. Store
and compare them as `string_view` when possible. Convert to `std::string` only when
you need to outlive the `FastDom` lifetime or concatenate values.

**Avoid `DOM_INSITU` on read-only mapped files.**
`mmap` returns read-only pages by default. Using `DOM_INSITU` on a file opened with
`MmapReader` will cause a segfault when the parser attempts to write null terminators
into the input. Either copy the file data to a writable buffer first, or use
`DOM_FAST` (without `DOM_INSITU`) on the const span.
