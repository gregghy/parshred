# Parshred Migration Guide

This guide covers the most common patterns needed to port code from libxml2,
pugixml, and RapidXML to parshred.

---

## From libxml2

### SAX Callback Mapping

libxml2 SAX uses a `xmlSAXHandler` struct with function pointers. Parshred uses
either `SaxParser` (with `std::function` callbacks) or `FastSaxParser` (with a
virtual `SaxHandler` subclass).

**libxml2:**

```c
#include <libxml/parser.h>

static void start_element_cb(void* ctx, const xmlChar* name,
                             const xmlChar** attrs) {
    printf("start: %s\n", name);
    // attrs is a flat array: name, value, name, value, ..., NULL
    for (int i = 0; attrs && attrs[i]; i += 2) {
        printf("  attr: %s = %s\n", attrs[i], attrs[i+1]);
    }
}

static void end_element_cb(void* ctx, const xmlChar* name) {
    printf("end: %s\n", name);
}

static void text_cb(void* ctx, const xmlChar* ch, int len) {
    printf("text: %.*s\n", len, ch);
}

xmlSAXHandler handler = {};
handler.startElement = start_element_cb;
handler.endElement   = end_element_cb;
handler.characters   = text_cb;

xmlSAXUserParseFile(&handler, NULL, "data.xml");
```

**Parshred (`SaxParser`):**

```cpp
#include <parshred/sax_parser.hpp>

parshred::SaxParser parser;

parser.on_start_element([](std::string_view name,
                           std::span<const parshred::Attribute> attrs) {
    printf("start: %.*s\n", (int)name.size(), name.data());
    for (const auto& a : attrs) {
        printf("  attr: %.*s = %.*s\n",
               (int)a.name.size(), a.name.data(),
               (int)a.value.size(), a.value.data());
    }
});

parser.on_end_element([](std::string_view name) {
    printf("end: %.*s\n", (int)name.size(), name.data());
});

parser.on_text([](std::string_view text) {
    printf("text: %.*s\n", (int)text.size(), text.data());
});

parser.parse_file("data.xml");
```

**Parshred (`FastSaxParser` with `SaxHandler` — preferred for performance):**

```cpp
#include <parshred/fast_sax.hpp>

struct MyHandler : parshred::SaxHandler {
    void on_start_element(std::string_view name,
                          const parshred::Attribute* attrs,
                          size_t nattrs) override {
        // same logic as above
    }
    void on_end_element(std::string_view name) override {}
    void on_text(std::string_view text) override {}
};

MyHandler h;
parshred::fast_parse_turbo("data.xml content", data_len, h);
// or: FastSaxParser<> p; p.parse_file("data.xml", h);
```

**libxml2 SAX callback to parshred mapping:**

| libxml2 callback | parshred equivalent |
|---|---|
| `startElement` | `on_start_element` |
| `endElement` | `on_end_element` |
| `characters` | `on_text` |
| `comment` | `on_comment` |
| `processingInstruction` | `on_processing_instruction` |
| `cdataBlock` | `on_cdata` |
| `startDocument` / `endDocument` | No equivalent; parse returns when done |
| `xmlDecl` | `on_xml_declaration` |
| `error` / `fatalError` | `ParseError` exception |

**Key differences:**
- libxml2 attribute list is a flat `const xmlChar**` terminated by `NULL`.
  Parshred passes `std::span<const Attribute>` (or `const Attribute*` + count).
- libxml2 strings are `const xmlChar*` (null-terminated). Parshred passes
  `std::string_view` (pointer + length, not null-terminated). Do not pass
  `string_view::data()` to functions that expect a C string without checking
  `string_view::size()` first.
- libxml2 expands entities before delivering text to callbacks. Parshred
  `Normal` mode also expands entities; `Turbo` mode does not.

---

### DOM Equivalent Operations

**libxml2:**

```c
xmlDocPtr doc = xmlReadFile("data.xml", NULL, 0);
xmlNodePtr root = xmlDocGetRootElement(doc);

// Iterate children
for (xmlNodePtr child = root->children; child; child = child->next) {
    if (child->type == XML_ELEMENT_NODE) {
        printf("child: %s\n", child->name);
        xmlChar* val = xmlGetProp(child, BAD_CAST "id");
        if (val) { printf("  id=%s\n", val); xmlFree(val); }
    }
}

xmlFreeDoc(doc);
```

**Parshred:**

```cpp
#include <parshred/dom_fast.hpp>

auto dom = parshred::fast_dom_parse(data, len);
const parshred::FastNode& root = dom.root();

for (const parshred::FastNode* child = dom.first_child(root);
     child != nullptr;
     child = dom.next_sibling(*child)) {
    if (child->type != 1) continue;  // 1 = element
    printf("child: %.*s\n", (int)dom.name(*child).size(), dom.name(*child).data());
    auto id = dom.attr(*child, "id");
    if (!id.empty()) printf("  id=%.*s\n", (int)id.size(), id.data());
}
```

**Key differences:**
- libxml2 `xmlGetProp` returns a `xmlChar*` that must be freed with `xmlFree`.
  Parshred `dom.attr()` returns a `string_view` — no allocation, no free.
- libxml2 type codes: `XML_ELEMENT_NODE = 1`, `XML_TEXT_NODE = 3`. Parshred
  `FastNode::type`: `1 = element`, `2 = text`, `3 = comment`, `6 = attribute`.
- libxml2 `xmlChar*` uses UTF-8 encoded strings just as parshred does.

---

## From pugixml

### DOM Traversal Mapping

**pugixml:**

```cpp
#include <pugixml.hpp>

pugi::xml_document doc;
doc.load_file("data.xml");

pugi::xml_node root = doc.document_element();
for (pugi::xml_node child : root.children()) {
    printf("child: %s\n", child.name());
    const char* id = child.attribute("id").value();
    printf("  id=%s\n", id);
}

// find first child with name
pugi::xml_node found = root.child("book");

// text content
const char* text = root.child("title").child_value();
```

**Parshred (FastDom):**

```cpp
#include <parshred/dom_fast.hpp>

auto dom = parshred::fast_dom_parse(data, len);
const parshred::FastNode& root = dom.root();

for (const parshred::FastNode* child = dom.first_child(root);
     child != nullptr;
     child = dom.next_sibling(*child)) {
    if (child->type != 1) continue;
    printf("child: %.*s\n", (int)dom.name(*child).size(), dom.name(*child).data());
    auto id = dom.attr(*child, "id");
    printf("  id=%.*s\n", (int)id.size(), id.data());
}

// find first child with name — manual scan
const parshred::FastNode* found = nullptr;
for (const parshred::FastNode* n = dom.first_child(root);
     n != nullptr; n = dom.next_sibling(*n)) {
    if (n->type == 1 && dom.name(*n) == "book") { found = n; break; }
}

// text content — get the first text child
std::string_view text;
for (const parshred::FastNode* n = dom.first_child(root);
     n != nullptr; n = dom.next_sibling(*n)) {
    if (n->type == 2) { text = dom.value(*n); break; }
}
```

**Parshred (XmlDocument — closer to pugixml API):**

```cpp
#include <parshred/dom_parser.hpp>

auto result = parshred::dom_parse(data, len);
parshred::XmlNode* root = result.doc.root();

// Range-for over element children (skips text/comment nodes)
for (parshred::XmlNode* child : parshred::XmlDocument::elements(root)) {
    printf("child: %.*s\n", (int)child->name.size(), child->name.data());
    auto id = parshred::XmlDocument::attr(child, "id");
    printf("  id=%.*s\n", (int)id.size(), id.data());
}

parshred::XmlNode* found = parshred::XmlDocument::find_child(root, "book");
```

**pugixml to parshred API mapping:**

| pugixml | Parshred `FastDom` | Parshred `XmlDocument` |
|---|---|---|
| `doc.document_element()` | `dom.root()` | `result.doc.root()` |
| `node.name()` | `dom.name(node)` | `node->name` |
| `node.value()` | `dom.value(node)` | `node->value` |
| `node.attribute("x").value()` | `dom.attr(node, "x")` | `XmlDocument::attr(node, "x")` |
| `node.children()` | `dom.first_child(n)` / `dom.next_sibling(n)` | `XmlDocument::children(node)` |
| `node.child("name")` | manual scan | `XmlDocument::find_child(node, "name")` |
| `node.child_value()` | first text child's `dom.value()` | iterate children for `NodeType::Text` |
| `node.first_child()` | `dom.first_child(node)` | `node->first_child` |
| `node.next_sibling()` | `dom.next_sibling(node)` | `node->next_sibling` |
| `node.parent()` | O(n) scan | `node->parent` |
| `node.type() == element_node` | `node.type == 1` | `node->is_element()` |
| `node.type() == text_node` | `node.type == 2` | `node->is_text()` |
| `doc.load_file()` | `fast_dom_parse()` via `MmapReader` | `dom_parse()` |
| `doc.load_buffer()` | `fast_dom_parse(data, len)` | `dom_parse(data, len)` |
| `doc.save_file()` / `doc.save(stream)` | `XmlWriter::serialize()` | `XmlWriter::serialize()` |

### XPath Differences

pugixml's XPath API:

```cpp
pugi::xpath_node_set nodes = doc.select_nodes("/catalog/book[@genre='fiction']");
for (const auto& xn : nodes) {
    printf("%s\n", xn.node().child_value("title"));
}
```

Parshred XPath API:

```cpp
#include <parshred/xpath.hpp>

auto dom = parshred::fast_dom_parse(data, len);
auto indices = parshred::xpath::evaluate(dom, "/catalog/book[@genre='fiction']");

for (uint32_t idx : indices) {
    // get text content of the title child
    std::string title = parshred::xpath::evaluate_string(
        dom, "/catalog/book[@genre='fiction']/title");
    printf("%s\n", title.c_str());
}
```

**Key differences:**
- pugixml XPath returns `xpath_node_set` with wrapped `xml_node` objects.
  Parshred XPath returns `std::vector<uint32_t>` — raw node indices into
  `FastDom::nodes`. Access via `dom.nodes[idx]`.
- pugixml XPath context is the document. Parshred XPath context is also the
  document for absolute paths (`/`), or the root element for relative paths.
- pugixml supports the full XPath 1.0 spec. Parshred covers a practical subset.
  See `conformance.md` for the XPath coverage table.

---

## From RapidXML

### In-Situ Parsing Equivalent

RapidXML's fastest parse mode (`parse_fastest`) is in-situ: it writes null bytes
into the input buffer to terminate strings, avoiding all string copies. Parshred
replicates this with `DOM_INSITU`.

**RapidXML:**

```cpp
#include <rapidxml.hpp>

// RapidXML requires a mutable, null-terminated copy of the input
std::vector<char> buf(xml.begin(), xml.end());
buf.push_back('\0');

rapidxml::xml_document<> doc;
doc.parse<rapidxml::parse_fastest>(buf.data());

rapidxml::xml_node<>* root = doc.first_node();
rapidxml::xml_node<>* child = root->first_node("item");
const char* id = child->first_attribute("id")->value();
```

**Parshred (`dom_parse<DOM_FASTEST>`):**

```cpp
#include <parshred/dom_parser.hpp>

// Parshred also needs a mutable buffer for DOM_INSITU
std::vector<char> buf(xml.begin(), xml.end());

auto result = parshred::dom_parse<parshred::DOM_FASTEST>(buf.data(), buf.size());
parshred::XmlNode* root = result.doc.root();
parshred::XmlNode* child = parshred::XmlDocument::find_child(root, "item");
std::string_view id = parshred::XmlDocument::attr(child, "id");
```

**Parshred (`fast_dom_parse<FDOM_FASTEST>`) — faster for most uses:**

```cpp
#include <parshred/dom_fast.hpp>

// fast_dom_parse does NOT mutate the buffer (input is const)
auto dom = parshred::fast_dom_parse<parshred::FDOM_FASTEST>(xml.data(), xml.size());

const parshred::FastNode& root = dom.root();
std::string_view id;
for (const parshred::FastNode* n = dom.first_child(root);
     n != nullptr; n = dom.next_sibling(*n)) {
    if (n->type == 1 && dom.name(*n) == "item") {
        id = dom.attr(*n, "id");
        break;
    }
}
```

### Node Access Patterns

**RapidXML node traversal:**

```cpp
// Iterate over siblings of same name
for (auto* node = root->first_node("book");
     node;
     node = node->next_sibling("book")) {
    printf("%s\n", node->first_attribute("id")->value());
}

// Attribute iteration
for (auto* attr = node->first_attribute(); attr; attr = attr->next_attribute()) {
    printf("%s = %s\n", attr->name(), attr->value());
}
```

**Parshred (`XmlDocument`):**

```cpp
// Iterate all element children (filter by name manually)
for (parshred::XmlNode* child : parshred::XmlDocument::elements(root)) {
    if (child->name != "book") continue;
    printf("%.*s\n", (int)parshred::XmlDocument::attr(child, "id").size(),
           parshred::XmlDocument::attr(child, "id").data());
}

// Attribute iteration
for (parshred::XmlNode* attr : parshred::XmlDocument::attributes(node)) {
    printf("%.*s = %.*s\n",
           (int)attr->name.size(), attr->name.data(),
           (int)attr->value.size(), attr->value.data());
}
```

**Parshred (`FastDom`):**

```cpp
// Iterate children and filter
for (const parshred::FastNode* child = dom.first_child(root);
     child != nullptr;
     child = dom.next_sibling(*child)) {
    if (child->type != 1 || dom.name(*child) != "book") continue;
    printf("%.*s\n", (int)dom.attr(*child, "id").size(),
           dom.attr(*child, "id").data());
}

// Attribute iteration
for (const parshred::FastNode* attr = dom.first_attr(root);
     attr != nullptr;
     attr = dom.next_sibling(*attr)) {
    printf("%.*s = %.*s\n",
           (int)dom.name(*attr).size(), dom.name(*attr).data(),
           (int)dom.value(*attr).size(), dom.value(*attr).data());
}
```

### RapidXML to Parshred API Mapping

| RapidXML | Parshred `XmlDocument` | Parshred `FastDom` |
|---|---|---|
| `doc.first_node()` | `result.doc.root()` | `dom.root()` |
| `node->name()` | `node->name` | `dom.name(node)` |
| `node->value()` | `node->value` | `dom.value(node)` |
| `node->first_node()` | `node->first_child` | `dom.first_child(node)` |
| `node->first_node("name")` | `XmlDocument::find_child(node, "name")` | manual scan |
| `node->next_sibling()` | `node->next_sibling` | `dom.next_sibling(node)` |
| `node->next_sibling("name")` | iterate + filter | iterate + filter |
| `node->parent()` | `node->parent` | O(n) scan |
| `node->first_attribute()` | `node->first_attr` | `dom.first_attr(node)` |
| `node->first_attribute("name")` | `XmlDocument::attr(node, "name")` | `dom.attr(node, "name")` |
| `attr->next_attribute()` | `attr->next_sibling` | `dom.next_sibling(attr)` |
| `doc.parse<flags>(buf)` | `dom_parse<DOM_FASTEST>(buf, len)` | `fast_dom_parse<FDOM_FASTEST>(buf, len)` |
| `parse_fastest` | `DOM_FASTEST` | `FDOM_FASTEST` |
| `parse_no_entity_translation` | `DOM_NO_ENTITIES` | (default; expansion opt-in) |
| `parse_no_data_nodes` | `DOM_NO_TEXT_NODES` | `FDOM_NO_TEXT` |

**Key differences from RapidXML:**
- RapidXML `name()` and `value()` return `char*` (null-terminated, into the
  mutated buffer). Parshred returns `std::string_view` (pointer + length, not
  null-terminated). This requires `%.*s` in printf or explicit `.size()` checks.
- RapidXML `next_sibling("name")` filters by name. Parshred has no built-in
  filtered sibling iteration; filter manually in a loop.
- RapidXML allocates nodes from an internal pool stored in the `xml_document`.
  Parshred `FastDom` uses a `malloc`-allocated flat array. Parshred
  `XmlDocument` uses a `NodePool` (bump allocator in 64 KB blocks).
- RapidXML is header-only. Parshred is a static library (`libparshred.a`) with
  SIMD backends compiled separately.

---

## Common Patterns Across All Parsers

### Counting all elements

**libxml2 SAX:**

```c
static int count = 0;
static void start_cb(void* ctx, const xmlChar* name, const xmlChar** attrs) { ++count; }
xmlSAXHandler h = {}; h.startElement = start_cb;
xmlSAXUserParseFile(&h, NULL, "data.xml");
```

**pugixml:**

```cpp
struct Counter : pugi::xml_tree_walker {
    size_t count = 0;
    bool for_each(pugi::xml_node& node) override {
        if (node.type() == pugi::node_element) ++count;
        return true;
    }
} c;
doc.traverse(c);
```

**RapidXML:** requires a recursive function; no built-in walker.

**Parshred (fastest — turbo SAX):**

```cpp
parshred::CountingHandler h;
parshred::fast_parse_turbo(data, len, h);
size_t count = h.elements;
```

**Parshred (FastDom — if tree also needed):**

```cpp
auto dom = parshred::fast_dom_parse<parshred::FDOM_FASTEST>(data, len);
size_t count = dom.element_count();
```

---

### Collecting all attribute values for a given attribute name

**Parshred:**

```cpp
auto dom = parshred::fast_dom_parse(data, len);
std::vector<std::string_view> values;

std::function<void(const parshred::FastNode&)> collect;
collect = [&](const parshred::FastNode& n) {
    if (n.type == 1) {
        auto v = dom.attr(n, "id");
        if (!v.empty()) values.push_back(v);
        for (const parshred::FastNode* child = dom.first_child(n);
             child; child = dom.next_sibling(*child)) {
            collect(*child);
        }
    }
};
collect(dom.root());
```

Or with XPath:

```cpp
auto values = parshred::xpath::evaluate_strings(dom, "//@id");
```

---

### Checking well-formedness without a third-party tool

**Parshred:**

```cpp
#include <parshred/dtd.hpp>

auto issues = parshred::check_wellformedness(xml_string);
if (issues.empty()) {
    puts("well-formed");
} else {
    for (const auto& s : issues) puts(s.c_str());
}
```

This checks tag matching only (no DTD required). For DTD-based validity, parse
the internal DTD subset with `parse_dtd()` then call `validate()`.
