/// @file fuzz_xpath.cpp
/// @brief libFuzzer harness for the XPath 1.0 evaluation engine.
///
/// Strategy
/// --------
/// A small, fixed XML document is parsed once into a FastDom tree.  The
/// fuzz input is then interpreted as an XPath expression string and fed to
/// parshred::xpath::evaluate().  This exercises:
///   - XPath expression tokenisation / parsing (parse_xpath)
///   - Axis traversal, predicate evaluation, function calls
///   - The full eval_step / NodeSet machinery against a real DOM
///
/// The document is intentionally rich (elements, attributes, text nodes,
/// namespaces, nested structure) so that every XPath axis has something to
/// match against.
///
/// Safety contract:
///   - no crashes
///   - no hangs (enforced by libFuzzer -timeout; also the XPath engine has
///     no loops that grow with input length beyond O(dom_size))
///   - no ASan / UBSan findings
///
/// Build (manual):
///   clang++ -std=c++20 -fsanitize=fuzzer,address,undefined \
///           -I../../include fuzz_xpath.cpp ../../build/fuzz/libparshred.a \
///           -o fuzz_xpath
///
/// Run:
///   ./fuzz_xpath corpus/ -max_total_time=60 -max_len=4096

#include <parshred/dom_fast.hpp>
#include <parshred/xpath.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

// ── Fixed document parsed once at startup ────────────────────────────────
//
// libFuzzer calls LLVMFuzzerInitialize() before the first
// LLVMFuzzerTestOneInput(), but a function-local static is simpler and
// equally correct here because FastDom is move-only (no copy).
//
// The document covers:
//   - multiple element depths (up to 4)
//   - attributes with values
//   - text nodes
//   - a processing instruction
//   - a comment
//   - a CDATA section
//   - sibling elements (a, b, c children of "list")
static const char kFixedXml[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<!-- fuzzer fixture document -->"
    "<catalog xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"2\">"
      "<?app-hint process=\"true\"?>"
      "<book id=\"bk001\" lang=\"en\" available=\"yes\">"
        "<dc:title>The C++ Programming Language</dc:title>"
        "<author first=\"Bjarne\" last=\"Stroustrup\">Stroustrup, B.</author>"
        "<price currency=\"USD\">52.99</price>"
        "<description><![CDATA[A <comprehensive> guide & reference.]]></description>"
      "</book>"
      "<book id=\"bk002\" lang=\"de\">"
        "<dc:title>Effektives C++</dc:title>"
        "<author first=\"Scott\" last=\"Meyers\">Meyers, S.</author>"
        "<price currency=\"EUR\">39.90</price>"
      "</book>"
      "<list>"
        "<item key=\"a\" pos=\"1\">alpha</item>"
        "<item key=\"b\" pos=\"2\">beta</item>"
        "<item key=\"c\" pos=\"3\">gamma</item>"
      "</list>"
    "</catalog>";

static parshred::FastDom make_fixed_dom() {
    return parshred::fast_dom_parse<0>(kFixedXml, sizeof(kFixedXml) - 1);
}

// Initialised once; the FastDom tree is read-only during fuzzing.
static const parshred::FastDom& fixed_dom() {
    static parshred::FastDom dom = make_fixed_dom();
    return dom;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Interpret the raw bytes as a UTF-8 / ASCII XPath expression.
    // We do not NUL-terminate because string_view carries the length.
    const std::string_view expr(reinterpret_cast<const char*>(data), size);

    const parshred::FastDom& dom = fixed_dom();

    // ── Evaluate XPath expression ─────────────────────────────────────────
    // parse_xpath() and evaluate() are header-only and must not crash on
    // any byte sequence.  We swallow every exception type because malformed
    // XPath expressions are not bugs.
    try {
        auto nodes = parshred::xpath::evaluate(dom, expr);

        // Touch the results so ASan can catch dangling-pointer issues in
        // the returned NodeSet.
        for (uint32_t idx : nodes) {
            if (idx < static_cast<uint32_t>(dom.node_count)) {
                [[maybe_unused]] auto name = dom.name(dom.nodes[idx]);
            }
        }
    } catch (const std::exception&) {
        // Any std exception (e.g., std::out_of_range from stoi on a huge
        // numeric predicate) is acceptable; re-throw nothing.
    } catch (...) {
        // Belt-and-suspenders: swallow anything else too, so the fuzzer
        // only flags true crashes (SIGSEGV, SIGABRT, etc.).
    }

    // ── Also exercise the string-result convenience wrappers ─────────────
    try {
        [[maybe_unused]] auto sv = parshred::xpath::evaluate_strings(dom, expr);
        [[maybe_unused]] auto s  = parshred::xpath::evaluate_string(dom, expr);
        [[maybe_unused]] auto n  = parshred::xpath::evaluate_count(dom, expr);
    } catch (const std::exception&) {
    } catch (...) {
    }

    return 0;
}
