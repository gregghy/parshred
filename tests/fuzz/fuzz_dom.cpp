/// @file fuzz_dom.cpp
/// @brief libFuzzer harness for the compact fast DOM parser.
///
/// Exercises fast_dom_parse<0> (full parse: text nodes, attributes,
/// entity expansion) and fast_dom_parse<FDOM_FASTEST> (element structure
/// only, maximum speed) against arbitrary byte sequences.
///
/// Safety contract:
///   - no crashes
///   - no hangs (enforced by libFuzzer -timeout)
///   - no ASan / UBSan findings
///
/// Build (manual):
///   clang++ -std=c++20 -fsanitize=fuzzer,address,undefined \
///           -I../../include fuzz_dom.cpp ../../build/fuzz/libparshred.a \
///           -o fuzz_dom
///
/// Run:
///   ./fuzz_dom corpus/ -max_total_time=60

#include <parshred/dom_fast.hpp>

#include <cstddef>
#include <cstdint>

/// Walk every node in the DOM so that address-sanitizer can catch any
/// out-of-bounds pointer dereferences that the parser might have created.
static void walk_dom(const parshred::FastDom& dom, uint32_t idx, int depth) {
    // Guard against cycles or corrupt trees produced by malformed input.
    if (idx == 0 || idx >= static_cast<uint32_t>(dom.node_count)) return;
    if (depth > 512) return;  // match parser's DEFAULT_MAX_DEPTH

    const parshred::FastNode& node = dom.nodes[idx];

    // Touch name and value — forces ASan to validate the pointers.
    [[maybe_unused]] auto name  = dom.name(node);
    [[maybe_unused]] auto value = dom.value(node);

    // Recurse into children and attributes.
    walk_dom(dom, node.first_child, depth + 1);
    walk_dom(dom, node.next_sibling, depth);
    walk_dom(dom, node.first_attr, depth + 1);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const char* xml = reinterpret_cast<const char*>(data);

    // ── Full parse: text nodes, attrs, entity expansion (Flags = 0) ──────
    {
        try {
            parshred::FastDom dom = parshred::fast_dom_parse<0>(xml, size);
            if (dom.node_count > 0) {
                // Walk from the document node (index 1).
                walk_dom(dom, 1, 0);
            }
        } catch (const parshred::ParseError&) {
            // Malformed input — expected.
        } catch (const std::bad_alloc&) {
            // libFuzzer caps RSS via -rss_limit_mb; treat as non-bug.
        } catch (const std::exception&) {
        }
    }

    // ── Fastest mode: skip text + comments, maximum throughput ────────────
    {
        try {
            parshred::FastDom dom =
                parshred::fast_dom_parse<parshred::FDOM_FASTEST>(xml, size);
            if (dom.node_count > 0) {
                walk_dom(dom, 1, 0);
            }
        } catch (const parshred::ParseError&) {
        } catch (const std::bad_alloc&) {
        } catch (const std::exception&) {
        }
    }

    return 0;
}
