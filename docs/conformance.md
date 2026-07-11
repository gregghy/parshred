# Parshred Conformance Table

---

## XML 1.0 Specification Coverage

Reference: https://www.w3.org/TR/xml/

| Spec section | Feature | Status | Notes |
|---|---|---|---|
| 2.1 | Well-Formed XML Documents | Supported | Tag matching, single root, proper nesting |
| 2.2 | Characters | Supported | UTF-8 validated; surrogates rejected |
| 2.3 | Common syntactic constructs | Supported | Whitespace, names, tokens |
| 2.4 | Character data | Supported | PCDATA, whitespace in content |
| 2.5 | Comments `<!-- -->` | Supported | All parsers; skippable via flags |
| 2.6 | Processing instructions `<?...?>` | Supported | Target and data separated |
| 2.7 | CDATA sections `<![CDATA[...]]>` | Supported | All parsers |
| 2.8 | Prolog and Document Type | Partial | XML declaration supported; DOCTYPE parsed but not loaded |
| 2.9 | Standalone document declaration | Parsed | Value passed to `on_xml_declaration` callback; not enforced |
| 3 | Logical Structures (elements) | Supported | Start tags, end tags, self-closing |
| 3.1 | Start-tags, end-tags, empty-element tags | Supported | |
| 3.2 | Element type declarations (`<!ELEMENT>`) | Parsed (internal DTD) | Content model stored as string; not validated against instance |
| 3.3 | Attribute-list declarations (`<!ATTLIST>`) | Parsed (internal DTD) | Type and default validated |
| 3.3.1 | Attribute types | Partial | CDATA, ID, IDREF, IDREFS, NMTOKEN, NMTOKENS, enumeration supported; NOTATION parsed, not resolved |
| 3.3.2 | Attribute defaults | Supported | `#REQUIRED` and `#IMPLIED` checked; `#FIXED` checked |
| 3.4 | Conditional sections | Not supported | `<![INCLUDE[...]]>` and `<![IGNORE[...]]>` skipped |
| 4.1 | Character and entity references | Supported | `&#NNN;`, `&#xHHH;`, predefined named entities |
| 4.2 | Entity declarations (`<!ENTITY>`) | Parsed (internal DTD) | General entity storage in `Dtd::entities` |
| 4.2.2 | External entities | Not supported | Security decision; external DTD fetch not performed |
| 4.3 | Parsed entities | Partial | Internal general entities only |
| 4.4 | XML Processor treatment of entities | Partial | Predefined entities expanded; user-defined require DTD parse |
| 4.5 | Construction of entity replacement text | Partial | General entities from internal DTD |
| 4.6 | Predefined entities | Supported | `&lt;`, `&gt;`, `&amp;`, `&apos;`, `&quot;` |
| 4.7 | Notation declarations | Parsed | Stored; not resolved |
| 5 | Conformance | Partial | Well-formedness enforced; validity checking requires `validate()` call |

---

## Encoding Support

| Encoding | Read | Write | Notes |
|---|---|---|---|
| UTF-8 | Yes | Yes | Native encoding; no transcoding performed |
| UTF-16 (BOM) | No | No | Not supported; convert to UTF-8 before parsing |
| UTF-16 LE/BE (no BOM) | No | No | Not supported |
| ISO-8859-1 | No | No | Not supported |
| US-ASCII | Yes | Yes | Strict subset of UTF-8; works transparently |
| Other | No | No | Not supported |

Character validation: surrogate code points (U+D800–U+DFFF) are rejected by
`encode_utf8_codepoint()` (used during entity expansion). Other non-character
code points above U+10FFFF are silently dropped. The parsers do not validate
that all bytes in the input form valid UTF-8 sequences; they operate
byte-by-byte on structural characters and pass text ranges directly as
`string_view` without re-encoding.

---

## Known Limitations

**External DTDs and external parsed entities**
Not supported. The parser does not fetch external resources. DTD declarations
with `SYSTEM` or `PUBLIC` identifiers are parsed syntactically but the external
resource is not loaded. This is a deliberate security decision: fetching external
resources would enable XXE (XML External Entity) attacks.

**Parameter entities**
Parsed syntactically in `parse_dtd()` (`is_parameter = true`), but not expanded.
Parameter entity references (`%name;`) inside the internal DTD subset are not
substituted.

**Conditional sections**
`<![INCLUDE[...]]>` and `<![IGNORE[...]]>` constructs are not supported. The
scanner will typically misparse the content.

**DTD validation completeness**
`validate()` checks required attributes, undeclared attributes, and enumeration
values. It does not validate element content models (the sequence/choice grammar
in `<!ELEMENT>` declarations) against the actual child element order.

**Attribute normalization**
Full attribute value normalization per XML 1.0 §3.3.3 is available only when
`FDOM_NORMALIZE` is passed to `fast_dom_parse`. The default parse mode (`0`)
and turbo SAX mode do not normalize attribute whitespace.

**In-situ parsing with const input**
`DOM_INSITU` requires a mutable `char*` buffer. Passing `const char*` or
`string_view` with `DOM_INSITU` is a compile-time error. Using in-situ mode
with memory-mapped read-only files causes undefined behaviour (segfault).

**`value_len` capped at 65535**
`FastNode::value_len` is a `uint16_t`. Text nodes or attribute values longer
than 65535 bytes are truncated in the stored length field. The underlying data
in the `values` buffer is not truncated; only the accessor `value()` will
return a shorter view. This affects extremely long attribute values or text
nodes.

**`FastDom` parent access is O(n)**
`FastDom` stores no parent pointers (saves 8 bytes per node). The XPath
`parent::` axis and `..` shorthand are implemented by scanning the entire node
array for the node whose `first_child` chain includes the target. For small
to medium trees this is acceptable; for very large trees it is O(n) per step.
Use `XmlDocument` (via `dom_parse`) if frequent parent traversal is needed,
as `XmlNode` includes a `parent` pointer.

**Maximum nesting depth**
`FastSaxParser` uses a fixed 256-entry stack. Elements nested deeper than 256
levels will not be parsed correctly (no error is thrown; the parser silently
caps the stack). `SaxParser` and `dom_parse` enforce `DEFAULT_MAX_DEPTH = 512`
and throw `SecurityError` if exceeded.

---

## XPath 1.0 Coverage

Reference: https://www.w3.org/TR/xpath/

| Feature | Status | Notes |
|---|---|---|
| Absolute path `/a/b` | Supported | Starts from document node |
| Relative path `a/b` | Supported | Starts from root element |
| Descendant `//a` | Supported | DFS traversal |
| Self `.` | Supported | |
| Parent `..` | Supported | O(n) scan in `FastDom` |
| Attribute axis `@attr` | Supported | |
| Wildcard `*` | Supported | Matches any element |
| Attribute wildcard `@*` | Supported | Matches any attribute |
| `text()` node test | Supported | |
| `comment()` node test | Supported | |
| `node()` node test | Supported | |
| Position predicate `[n]` | Supported | 1-based |
| Last position `[last()]` | Supported | |
| Attribute existence `[@attr]` | Supported | |
| Attribute equality `[@attr='val']` | Supported | |
| Text equality `[text()='val']` | Supported | |
| `following-sibling::` axis | Supported | |
| `ancestor::` axis | Parsed | Not yet evaluated |
| `ancestor-or-self::` axis | Parsed | Not yet evaluated |
| `preceding-sibling::` axis | Parsed | Not yet evaluated |
| `following::` axis | Not supported | |
| `preceding::` axis | Not supported | |
| `namespace::` axis | Not supported | |
| `count()` function | Supported | Via `evaluate_count()` |
| `contains()` function | Not supported in predicates | |
| `starts-with()` function | Not supported in predicates | |
| `string-length()` function | Not supported in predicates | |
| `name()` function | Not supported in predicates | |
| `position()` function | Not supported in predicates | |
| Boolean operators `and`, `or`, `not()` | Not supported | |
| Arithmetic operators | Not supported | |
| Union operator `|` | Not supported | |
| Variable references `$var` | Not supported | |
| Abbreviated syntax `//` | Supported | Expands to `descendant-or-self::node()/` |

The XPath engine supports the most common document navigation patterns. It is
not a complete XPath 1.0 implementation. Applications requiring the full spec
should pre-process expressions using a conformant XPath library before using
parshred's engine for the matching step.

---

## XML Namespaces 1.0 Coverage

Reference: https://www.w3.org/TR/xml-names/

| Feature | Status | Notes |
|---|---|---|
| `xmlns:prefix="uri"` declarations | Supported | Parsed and stored in `NsContext` |
| Default namespace `xmlns="uri"` | Supported | Empty-string prefix in `NsContext` |
| Prefix resolution | Supported | `NsContext::resolve_name()` |
| Scope management (push/pop) | Supported | `push_scope()` / `pop_scope()` |
| Prefix shadowing in nested scopes | Supported | Most-recent binding wins |
| `xml:` prefix pre-declared | Supported | Always bound to `http://www.w3.org/XML/1998/namespace` |
| `xmlns:` prefix binding | Supported | URI stored in `ns::XMLNS` |
| Attribute namespace resolution | Supported | Call `resolve_name()` on attribute names |
| Automatic integration with SAX callbacks | Not automatic | Requires manual use of `NsContext` inside handler |
| Automatic integration with DOM tree | Not supported | Namespace URIs are not stored in `FastNode`; resolve manually from attribute names |
| Namespace-aware XPath evaluation | Not supported | XPath engine matches on lexical names; does not resolve namespace URIs |

Namespace processing is intentionally decoupled from the core parsers to avoid
overhead in applications that do not use namespaces. Integrate by constructing an
`NsContext` inside your `SaxHandler` and calling `process_element_ns()` on each
start-element event.
