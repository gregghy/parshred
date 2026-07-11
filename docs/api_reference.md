# Parshred API Reference

Version 0.1.0 — C++20 required.

---

## Public Headers

| Header | Purpose |
|---|---|
| `parshred/parshred.hpp` | Umbrella header, includes all of the below |
| `parshred/common.hpp` | `Token`, `Attribute`, `ParseError`, `TokenType`, security constants |
| `parshred/sax_parser.hpp` | `SaxParser` — callback-based SAX API |
| `parshred/fast_sax.hpp` | `FastSaxParser`, `SaxHandler`, `fast_parse()`, `fast_parse_turbo()` |
| `parshred/dom_fast.hpp` | `FastDom`, `FastNode`, `fast_dom_parse()`, `FDOM_*` flags |
| `parshred/dom_parser.hpp` | `dom_parse()`, `DomParseResult`, `DOM_*` flags |
| `parshred/dom.hpp` | `XmlDocument`, `XmlNode`, `NodeType`, range helpers |
| `parshred/xpath.hpp` | `xpath::evaluate()`, `xpath::XPathValue`, `xpath::NodeSet` |
| `parshred/writer.hpp` | `XmlWriter`, `DomBuilder`, `WriteOptions`, `escape_text()` |
| `parshred/namespace.hpp` | `NsContext`, `QName`, `process_element_ns()` |
| `parshred/pipeline.hpp` | `ChunkedParser`, `ParallelParser`, `PipelineConfig` |
| `parshred/mmap_reader.hpp` | `MmapReader` — memory-mapped file I/O |
| `parshred/arena.hpp` | `Arena` — bump allocator |
| `parshred/dtd.hpp` | `Dtd`, `parse_dtd()`, `validate()`, `check_wellformedness()` |
| `parshred/simd_scanner.hpp` | `simd_scan()`, `StructuralIndex` (internal/advanced) |
| `parshred/tokenizer.hpp` | `Tokenizer` (internal/advanced) |

---

## Parsing

### `SaxParser` — `parshred/sax_parser.hpp`

Callback-based SAX parser. Callbacks are `std::function<>` objects registered before
parsing. All `string_view` values in callbacks are zero-copy references into the
original input buffer and are valid only for the duration of the callback.

```cpp
class SaxParser {
public:
    // Callback registration
    void on_start_element(StartElementCb cb);  // (string_view name, span<Attribute> attrs)
    void on_end_element(EndElementCb cb);      // (string_view name)
    void on_text(TextCb cb);                   // (string_view text)
    void on_comment(CommentCb cb);             // (string_view text)
    void on_processing_instruction(PICb cb);   // (string_view target, string_view data)
    void on_cdata(CDataCb cb);                 // (string_view text)
    void on_xml_declaration(XmlDeclCb cb);     // (string_view version, encoding, standalone)

    // Parsing
    void parse(std::span<const char> input);
    void parse_file(const std::string& path);
    void parse_string(std::string_view str);

    // Security limits
    void set_max_depth(size_t depth);              // default: 512
    void set_max_entity_expansions(size_t n);      // default: 10,000
    void set_max_attribute_count(size_t n);        // default: 1,000

    // Statistics (accumulated across parse calls)
    const Stats& stats() const noexcept;
    void reset_stats() noexcept;

    struct Stats {
        size_t elements;
        size_t attributes;
        size_t text_nodes;
        size_t comments;
        size_t cdata_nodes;
        size_t bytes_parsed;
    };
};
```

**Performance note:** `SaxParser` uses `std::function` for callbacks which carries
indirect call overhead. For performance-critical code, prefer `FastSaxParser` with
the `SaxHandler` template API.

---

### `FastSaxParser` — `parshred/fast_sax.hpp`

Fused single-pass SAX parser. The parse loop scans the input directly without
building an intermediate token vector. Template-based handlers are devirtualized
and inlined into the parse loop by the compiler.

```cpp
enum class ParseMode : uint32_t {
    Normal,  // entity expansion, depth checks, statistics
    Turbo,   // no entity expansion, no depth checks, no stats
};

template<ParseMode Mode = ParseMode::Normal>
class FastSaxParser {
public:
    // Parse from pointer/length
    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    // Parse from span
    template<typename Handler>
    void parse(std::span<const char> input, Handler& handler);

    // Parse from string/string_view
    template<typename Handler>
    void parse_string(std::string_view input, Handler& handler);

    // Parse from file (uses MmapReader internally)
    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler);

    // Parse a chunk without resetting state (for ChunkedParser)
    template<typename Handler>
    void parse_chunk(const char* data, size_t len, Handler& handler);

    // Security limits (Normal mode only)
    void set_max_depth(size_t d);
    void set_max_entity_expansions(size_t n);
    void set_max_attribute_count(size_t n);

    const Stats& stats() const noexcept;
};
```

**`SaxHandler` base class** — override only the events you need:

```cpp
struct SaxHandler {
    virtual void on_start_element(std::string_view name,
                                  const Attribute* attrs, size_t nattrs);
    virtual void on_end_element(std::string_view name);
    virtual void on_text(std::string_view text);
    virtual void on_cdata(std::string_view text);
    virtual void on_comment(std::string_view text);
    virtual void on_processing_instruction(std::string_view target,
                                           std::string_view data);
    virtual void on_xml_declaration(std::string_view version,
                                    std::string_view encoding,
                                    std::string_view standalone);
};

struct NullHandler    final : SaxHandler {};   // no-op
struct CountingHandler final : SaxHandler {    // counts elements/attributes/text
    size_t elements = 0;
    size_t end_tags = 0;
    size_t text_nodes = 0;
    size_t attributes = 0;
};
```

**Free function shortcuts:**

```cpp
// Turbo mode (fastest — no entity expansion)
template<typename Handler>
void fast_parse_turbo(const char* data, size_t len, Handler& handler);

template<typename Handler>
void fast_parse_turbo(std::string_view input, Handler& handler);

// Normal mode
template<typename Handler>
void fast_parse(const char* data, size_t len, Handler& handler);

template<typename Handler>
void fast_parse(std::string_view input, Handler& handler);
```

**`FastSaxParserEasy`** — `std::function` wrapper for easier migration:

```cpp
class FastSaxParserEasy {
public:
    void on_start_element(StartElementCb cb);  // span<Attribute> variant
    void on_end_element(EndElementCb cb);
    void on_text(TextCb cb);
    void on_comment(CommentCb cb);
    void on_cdata(CDataCb cb);
    void set_max_depth(size_t d);
    void set_max_entity_expansions(size_t n);
    void set_max_attribute_count(size_t n);
    void parse_string(std::string_view input);
    void parse_file(const std::string& path);
    const Stats& stats() const noexcept;
};
```

---

## DOM

### `fast_dom_parse` — `parshred/dom_fast.hpp`

Parses XML into a `FastDom`: a flat `malloc`-allocated array of 32-byte `FastNode`
records. No virtual dispatch, no per-node heap allocation — the entire tree is one
contiguous array.

```cpp
// Parse flags (combinable with |)
inline constexpr unsigned FDOM_NO_TEXT    = 1u << 0;  // skip text nodes
inline constexpr unsigned FDOM_NO_ATTRS   = 1u << 1;  // skip attributes
inline constexpr unsigned FDOM_NO_COMMENT = 1u << 2;  // skip comments
inline constexpr unsigned FDOM_NORMALIZE  = 1u << 3;  // expand entities + normalize attrs
inline constexpr unsigned FDOM_FASTEST    = FDOM_NO_TEXT | FDOM_NO_COMMENT;

template<unsigned Flags = 0>
FastDom fast_dom_parse(const char* data, size_t len);
```

**`FastNode`** — 32-byte compact node:

```cpp
struct FastNode {
    const char* name_ptr;       // pointer into source buffer
    uint32_t    name_len;
    uint32_t    first_child;    // index into FastDom::nodes (0 = none)
    uint32_t    next_sibling;   // index into FastDom::nodes (0 = none)
    uint32_t    first_attr;     // index into FastDom::nodes (0 = none)
    uint32_t    value_offset;   // offset into values buffer
    uint16_t    value_len;
    uint8_t     type;           // 0=document, 1=element, 2=text, 3=comment,
                                // 4=cdata, 5=PI, 6=attribute
    uint8_t     flags;
};
```

**`FastDom`** — tree container and accessor:

```cpp
struct FastDom {
    FastNode*         nodes;        // flat array
    size_t            node_count;
    size_t            capacity;
    std::vector<char> values;       // text/value storage buffer
    const char*       data_ptr;     // original input (for zero-copy attr values)
    uint32_t          root_idx;     // index of root element

    // Accessors (return string_view — zero copy)
    const FastNode& root() const noexcept;
    std::string_view name(const FastNode& n) const noexcept;
    std::string_view value(const FastNode& n) const noexcept;

    // Navigation (return nullptr when no match)
    const FastNode* first_child(const FastNode& n) const noexcept;
    const FastNode* next_sibling(const FastNode& n) const noexcept;
    const FastNode* first_attr(const FastNode& n) const noexcept;

    // Attribute lookup by name
    std::string_view attr(const FastNode& elem, std::string_view name) const noexcept;

    size_t element_count() const noexcept;
};
```

---

### `dom_parse` — `parshred/dom_parser.hpp`

Full-featured DOM parser producing an `XmlDocument` with `XmlNode` (80-byte) nodes
allocated from a `NodePool`. Supports range-based for iteration.

```cpp
// Parse flags
inline constexpr unsigned DOM_NO_ENTITIES    = 1u << 0;
inline constexpr unsigned DOM_NO_TEXT_NODES  = 1u << 1;
inline constexpr unsigned DOM_NO_COMMENTS    = 1u << 2;
inline constexpr unsigned DOM_NO_PIS         = 1u << 3;
inline constexpr unsigned DOM_NO_DOCTYPE     = 1u << 4;
inline constexpr unsigned DOM_NO_DECLARATIONS = 1u << 5;
inline constexpr unsigned DOM_INSITU         = 1u << 6;  // mutate input buffer

// Preset combinations
inline constexpr unsigned DOM_FASTEST = /* all skips + INSITU */;
inline constexpr unsigned DOM_FAST    = /* no entities + no comments + INSITU */;
inline constexpr unsigned DOM_DEFAULT = 0;

struct DomParseResult {
    XmlDocument doc;
    NodePool    pool;
    StringPool  strings;  // arena for entity-expanded values
};

template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(char* data, size_t len);          // mutable: supports DOM_INSITU

template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(const char* data, size_t len);    // const: DOM_INSITU disallowed

template<unsigned Flags = DOM_DEFAULT>
DomParseResult dom_parse(std::string_view input);          // DOM_INSITU disallowed
```

**`XmlDocument`** navigation:

```cpp
class XmlDocument {
public:
    XmlNode* root() const noexcept;
    XmlNode* document_node() noexcept;

    // Static navigation helpers
    static XmlNode* first_element(XmlNode* node) noexcept;
    static XmlNode* next_element(XmlNode* node) noexcept;
    static XmlNode* find_child(XmlNode* node, std::string_view name) noexcept;
    static std::string_view attr(XmlNode* node, std::string_view name) noexcept;

    // Range-for support
    static ChildRange children(XmlNode* node) noexcept;   // all children
    static ChildRange elements(XmlNode* node) noexcept;   // element children only
    static AttrRange  attributes(XmlNode* node) noexcept; // attributes

    size_t node_count() const noexcept;
    size_t bytes_allocated() const noexcept;
};
```

**`XmlNode`** — 80-byte node:

```cpp
struct XmlNode {
    std::string_view name;          // tag name or attr name
    std::string_view value;         // text content or attr value
    NodeType         type;
    uint8_t          flags;
    XmlNode*         parent;
    XmlNode*         first_child;
    XmlNode*         last_child;
    XmlNode*         next_sibling;
    XmlNode*         first_attr;

    bool is_element() const noexcept;
    bool is_text() const noexcept;
    bool has_children() const noexcept;
    bool has_attrs() const noexcept;
};

enum class NodeType : uint8_t {
    Document, Element, Text, CData, Comment, PI, Attribute, Declaration, DocType
};
```

---

## XPath

All XPath functions live in the `parshred::xpath` namespace and operate on `FastDom`
trees. Requires `#include <parshred/xpath.hpp>`.

```cpp
namespace parshred::xpath {

// Primary evaluation function — returns node indices
NodeSet evaluate(const FastDom& dom, std::string_view expr);

// Convenience wrappers
std::vector<std::string> evaluate_strings(const FastDom& dom, std::string_view expr);
std::string              evaluate_string (const FastDom& dom, std::string_view expr);
size_t                   evaluate_count  (const FastDom& dom, std::string_view expr);

// Parsed AST (for repeated evaluation of the same expression)
XPathExpr parse_xpath(std::string_view expr);
NodeSet   eval_step(const FastDom& dom, const NodeSet& context, const Step& step);

// Text content of an element (concatenation of all text descendants)
std::string get_text_content(const FastDom& dom, uint32_t node_idx);

} // namespace parshred::xpath
```

**`NodeSet`** is `std::vector<uint32_t>` — indices into `FastDom::nodes`.

**`XPathValue`** — typed result for function calls:

```cpp
struct XPathValue {
    std::variant<NodeSet, std::string, double, bool> data;

    bool is_nodeset() const;
    bool is_string() const;
    bool is_number() const;
    bool is_boolean() const;

    const NodeSet&   as_nodeset() const;
    const std::string& as_string() const;
    double           as_number() const;
    bool             as_boolean() const;

    std::string to_string(const FastDom& dom) const;  // XPath string() semantics
    bool        to_boolean() const;                   // XPath boolean() semantics
};
```

---

## Namespaces

```cpp
// parshred/namespace.hpp

struct QName {
    std::string_view local_name;
    std::string_view prefix;
    std::string_view namespace_uri;
};

namespace ns {
    constexpr std::string_view XML   = "http://www.w3.org/XML/1998/namespace";
    constexpr std::string_view XMLNS = "http://www.w3.org/2000/xmlns/";
}

class NsContext {
public:
    NsContext();  // pre-declares xml: prefix

    void push_scope() noexcept;
    void pop_scope() noexcept;

    void declare(std::string_view prefix, std::string_view uri);

    std::string_view resolve(std::string_view prefix) const noexcept;
    QName resolve_name(std::string_view qname) const noexcept;

    // Processes xmlns:prefix="uri" attributes, declares into current scope
    size_t process_declarations(
        const std::pair<std::string_view, std::string_view>* attrs,
        size_t nattrs);

    const std::vector<NsBinding>& bindings() const noexcept;
    size_t depth() const noexcept;
    void reset();
};

// Helper for use inside SaxHandler::on_start_element
QName process_element_ns(std::string_view name,
                         const Attribute* attrs, size_t nattrs,
                         NsContext& ctx);
// Call ctx.pop_scope() in on_end_element to match.
```

---

## Validation

```cpp
// parshred/dtd.hpp

// Parse the internal DTD subset (content between [ and ] in <!DOCTYPE name [...]>)
Dtd parse_dtd(std::string_view content);

struct Dtd {
    std::string root_name;
    std::unordered_map<std::string, EntityDecl>              entities;
    std::unordered_map<std::string, ElementDecl>             elements;
    std::unordered_map<std::string, std::vector<AttrDecl>>   attlists;

    const EntityDecl*             find_entity(std::string_view name) const;
    const ElementDecl*            find_element(std::string_view name) const;
    const std::vector<AttrDecl>*  find_attlist(std::string_view elem) const;
};

struct ValidationError {
    enum class Kind {
        UndeclaredElement, UndeclaredAttribute,
        RequiredAttributeMissing, InvalidAttributeValue,
        InvalidContent, RootMismatch
    };
    Kind kind;
    std::string element;
    std::string detail;
};

std::vector<ValidationError> validate(
    const Dtd& dtd,
    const std::vector<std::pair<std::string_view,
                                std::vector<std::pair<std::string_view,
                                                      std::string_view>>>>& elements);

// Well-formedness check (tag matching only, no DTD required)
std::vector<std::string> check_wellformedness(std::string_view xml);
```

---

## Writing

### `XmlWriter` — `parshred/writer.hpp`

Serializes a `FastDom` tree to an XML string.

```cpp
struct WriteOptions {
    bool        pretty          = true;     // enable indentation
    std::string indent          = "  ";     // indent unit (two spaces)
    bool        xml_declaration = true;     // emit <?xml ...?>
    std::string encoding        = "UTF-8";
    bool        self_close_empty = true;    // <br/> vs <br></br>
    bool        escape_entities = true;     // escape &, <, >, " in output
};

class XmlWriter {
public:
    explicit XmlWriter(const WriteOptions& opts = {});

    // Serialize the entire document (from root element)
    std::string serialize(const FastDom& dom) const;

    // Serialize a single node and its subtree
    std::string serialize_node_str(const FastDom& dom, uint32_t idx) const;
};

// Entity escaping utilities
std::string escape_text(std::string_view text);  // escapes &, <, >
std::string escape_attr(std::string_view text);  // escapes &, <, ", '
```

### `DomBuilder` — `parshred/writer.hpp`

Builds a `FastDom` tree programmatically. The builder uses an internal stack to track
the current open element.

```cpp
class DomBuilder {
public:
    DomBuilder();

    uint32_t start_element(std::string_view name);  // opens element, returns index
    void     end_element();                          // closes current element
    uint32_t add_element(std::string_view name);    // self-closing element

    void add_attribute(std::string_view name, std::string_view value);
    void add_text(std::string_view text);
    void add_comment(std::string_view text);

    FastDom build();  // consumes builder, returns completed tree
};
```

### Tree Modification

```cpp
// Remove a child node from parent (unlinks from sibling chain)
void remove_child(FastDom& dom, uint32_t parent_idx, uint32_t child_idx);

// Remove an attribute from an element
void remove_attribute(FastDom& dom, uint32_t elem_idx, std::string_view attr_name);
```

---

## Utilities

### `MmapReader` — `parshred/mmap_reader.hpp`

Zero-copy file access via `mmap(2)`. Files below 4096 bytes fall back to a
heap buffer.

```cpp
class MmapReader {
public:
    static constexpr size_t MMAP_THRESHOLD = 4096;

    void open(const std::string& path);      // throws IOError on failure
    void load_buffer(const char* data, size_t size);
    void load_string(std::string_view str);
    void load_stdin();

    std::span<const char> data() const noexcept;
    size_t                size() const noexcept;
    bool                  is_open() const noexcept;
    void                  close() noexcept;
};
```

### `Arena` — `parshred/arena.hpp`

Bump allocator for temporary parsing state. O(1) allocation, O(1) bulk free via
`reset()`. Not thread-safe.

```cpp
class Arena {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024;

    explicit Arena(size_t block_size = DEFAULT_BLOCK_SIZE);

    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    template<typename T, typename... Args>
    T* create(Args&&... args);

    template<typename T>
    T* allocate_array(size_t count);

    void   reset() noexcept;
    size_t total_allocated() const noexcept;
    size_t block_count() const noexcept;
};
```

### `ChunkedParser` / `ParallelParser` — `parshred/pipeline.hpp`

For files too large to map fully, or to overlap I/O prefetch with parsing.

```cpp
struct PipelineConfig {
    size_t chunk_size  = 2 * 1024 * 1024;  // 2 MB per chunk
    size_t overlap     = 64 * 1024;         // 64 KB overlap for boundary tags
    bool   prefetch    = true;              // madvise prefetch
    size_t num_threads = 0;                 // 0 = std::thread::hardware_concurrency()
};

template<ParseMode Mode = ParseMode::Turbo>
class ChunkedParser {
public:
    explicit ChunkedParser(PipelineConfig config = {});

    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler);

    size_t bytes_processed() const noexcept;
};

template<ParseMode Mode = ParseMode::Turbo>
class ParallelParser {
public:
    explicit ParallelParser(PipelineConfig config = {});

    template<typename Handler>
    void parse_file(const std::string& path, Handler& handler);

    template<typename Handler>
    void parse(const char* data, size_t len, Handler& handler);

    size_t bytes_processed() const noexcept;
};
```

### Common Types — `parshred/common.hpp`

```cpp
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

class ParseError : public std::runtime_error {
    size_t offset() const noexcept;  // byte offset where error occurred
};
class SecurityError : public ParseError {};
class IOError       : public std::runtime_error {};

struct Token {
    TokenType        type;
    std::string_view text;    // zero-copy view into input
    size_t           offset;  // byte offset in input
};

struct Attribute {
    std::string_view name;
    std::string_view value;
};

inline constexpr size_t DEFAULT_MAX_DEPTH             = 512;
inline constexpr size_t DEFAULT_MAX_ENTITY_EXPANSIONS = 10'000;
inline constexpr size_t DEFAULT_MAX_ATTRIBUTE_COUNT   = 1'000;
```

---

## Performance Notes

| Use case | Recommended API | Typical throughput |
|---|---|---|
| Large file, no tree needed | `fast_parse_turbo()` + `SaxHandler` | Highest — fused single pass |
| Large file, with entity expansion | `FastSaxParser<Normal>` | High |
| Large file (>1 GB), streaming | `ChunkedParser<Turbo>` | Constant memory |
| Very large file, parallel I/O | `ParallelParser<Turbo>` | Overlapped I/O + parse |
| Small-medium file, need tree | `fast_dom_parse<FDOM_FASTEST>()` | 32-byte nodes, flat array |
| Need XPath queries | `fast_dom_parse()` + `xpath::evaluate()` | Depends on query |
| Need DOM_INSITU (RapidXML-style) | `dom_parse<DOM_FASTEST>()` | In-situ mutation |
| Need full XmlDocument traversal | `dom_parse<DOM_DEFAULT>()` | Slower, richer API |
| Build XML programmatically | `DomBuilder` + `XmlWriter` | N/A |

`FDOM_FASTEST` skips text nodes and comments and does not expand entities — use it
when you only need element structure and attribute values. `FDOM_NORMALIZE` enables
entity expansion and attribute whitespace normalization per the XML 1.0 spec.

For files below ~64 KB, DOM parsing is generally faster than SAX due to better
branch prediction on short inputs. For files above ~1 MB, SAX or chunked streaming
uses less memory with comparable speed.
