# Parshred Quick Start Guide

Get up and running with parshred XML parsing in under five minutes.

## Installation

**Requirements:** GCC 12+ or Clang 15+, CMake 3.20+, x86-64 with SSE 4.2 minimum.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(parshred
    GIT_REPOSITORY https://github.com/yourorg/parshred.git
    GIT_TAG v0.1.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(parshred)
target_link_libraries(my_target PRIVATE parshred)
```

### Building from Source

```bash
git clone https://github.com/yourorg/parshred.git && cd parshred
cmake --preset release && cmake --build build/release
# static library: build/release/src/libparshred.a
```

Manual link: `g++ -std=c++20 -O3 -mavx2 -I<repo>/include myapp.cpp -L<repo>/build/release/src -lparshred -o myapp`

---

## Headers

```cpp
#include <parshred/parshred.hpp>    // umbrella — includes all headers below
#include <parshred/sax_parser.hpp>  // SaxParser (std::function callbacks)
#include <parshred/fast_sax.hpp>    // FastSaxParser, fast_parse(), SaxHandler
#include <parshred/dom_fast.hpp>    // fast_dom_parse(), FastDom
#include <parshred/dom_parser.hpp>  // dom_parse(), XmlDocument
#include <parshred/xpath.hpp>       // xpath::evaluate()
#include <parshred/writer.hpp>      // XmlWriter, DomBuilder
#include <parshred/namespace.hpp>   // NsContext, QName
#include <parshred/pipeline.hpp>    // ChunkedParser, ParallelParser
```

---

## SAX Parsing

Callbacks receive zero-copy `string_view` references into the original input.

```cpp
#include <parshred/sax_parser.hpp>

parshred::SaxParser parser;

parser.on_start_element([](std::string_view name,
                           std::span<const parshred::Attribute> attrs) {
    for (const auto& a : attrs)
        // a.name and a.value are string_view — zero copy
        (void)a;
});
parser.on_end_element([](std::string_view name) {});
parser.on_text([](std::string_view text) {});

parser.parse_file("data.xml");
// parser.stats().elements, .bytes_parsed, etc.
```

For maximum throughput, use the template-based `SaxHandler` API. The compiler inlines
callbacks directly into the parse loop, eliminating `std::function` overhead.

```cpp
#include <parshred/fast_sax.hpp>

struct MyHandler : parshred::SaxHandler {
    size_t count = 0;
    void on_start_element(std::string_view name,
                          const parshred::Attribute* attrs,
                          size_t nattrs) override { ++count; }
};

MyHandler h;
parshred::FastSaxParser<parshred::ParseMode::Turbo> p;
p.parse_file("data.xml", h);       // turbo: no entity expansion, max speed
// or: parshred::fast_parse_turbo(data, len, h);
```

---

## DOM Parsing

`fast_dom_parse` builds a compact flat tree (32-byte nodes, one `malloc` call).

```cpp
#include <parshred/dom_fast.hpp>

std::string xml = "<library><book id=\"1\" genre=\"fiction\"><title>Dune</title>"
                  "</book></library>";

parshred::FastDom dom = parshred::fast_dom_parse(xml.data(), xml.size());
const parshred::FastNode& root = dom.root();   // "library"

for (const parshred::FastNode* child = dom.first_child(root);
     child; child = dom.next_sibling(*child)) {
    if (child->type != 1) continue;            // 1 = element
    std::string_view id    = dom.attr(*child, "id");
    std::string_view genre = dom.attr(*child, "genre");
    // dom.name(*child), dom.value(*child), dom.first_child(*child), ...
}
```

For the full `XmlDocument` API with range-based for and parent pointers:

```cpp
#include <parshred/dom_parser.hpp>

auto result = parshred::dom_parse("<root><child attr=\"v\">text</child></root>");
parshred::XmlNode* root = result.doc.root();

for (parshred::XmlNode* child : parshred::XmlDocument::elements(root)) {
    std::string_view val   = parshred::XmlDocument::attr(child, "attr");
    parshred::XmlNode* sub = parshred::XmlDocument::find_child(root, "child");
}
```

---

## XPath Queries

XPath operates on `FastDom` trees. Results are node indices into `dom.nodes`.

```cpp
#include <parshred/xpath.hpp>

auto dom = parshred::fast_dom_parse(xml.data(), xml.size());

// Returns vector<uint32_t> of matching node indices
auto idx = parshred::xpath::evaluate(dom, "/library/book[@genre='fiction']");

// Convenience wrappers
auto titles = parshred::xpath::evaluate_strings(dom, "//title");     // vector<string>
std::string first = parshred::xpath::evaluate_string(dom, "/library/book[1]/title");
size_t      n     = parshred::xpath::evaluate_count(dom, "//book");
```

Supported syntax: `/a/b`, `//a`, `.`, `..`, `@attr`, `*`, `@*`, `[n]`, `[@attr]`,
`[@attr='val']`, `[text()='val']`, `text()`, `comment()`, `node()`.

---

## Building XML with DomBuilder

```cpp
#include <parshred/writer.hpp>

parshred::DomBuilder b;
b.start_element("catalog");
    b.add_attribute("version", "1.0");
    b.start_element("product");
        b.add_attribute("id", "p42");
        b.add_text("Widget");
    b.end_element();
b.end_element();

parshred::FastDom dom = b.build();
```

---

## Serializing to String

```cpp
#include <parshred/writer.hpp>

parshred::XmlWriter writer;                      // pretty + XML declaration (defaults)
std::string xml_out = writer.serialize(dom);

parshred::WriteOptions opts;
opts.pretty = false; opts.xml_declaration = false;
std::string compact = parshred::XmlWriter(opts).serialize(dom);

// Escaping utilities
std::string t = parshred::escape_text("a < b & c");   // → "a &lt; b &amp; c"
std::string a = parshred::escape_attr("say \"hi\"");  // → "say &quot;hi&quot;"
```

---

## Error Handling

```cpp
try {
    parshred::SaxParser p;
    p.parse_file("bad.xml");
} catch (const parshred::ParseError& e) {
    // Malformed XML — e.offset() gives the byte where parsing failed
} catch (const parshred::SecurityError& e) {
    // Depth or entity expansion limit exceeded
} catch (const parshred::IOError& e) {
    // File not found or mmap failure
}
```

Security limits (all configurable via `set_max_*` on `SaxParser` / `FastSaxParser`):

| Limit | Default |
|---|---|
| Maximum nesting depth | 512 |
| Maximum entity expansions per parse | 10,000 |
| Maximum attributes per element | 1,000 |
