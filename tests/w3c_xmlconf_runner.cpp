/// @file w3c_xmlconf_runner.cpp
/// @brief W3C XML Conformance Test Suite runner for parshred.
///
/// Downloads/runs the official W3C XML Conformance Test Suite
/// (https://www.w3.org/XML/Test/) against parshred and emits a JUnit XML
/// report + a stdout summary.
///
/// Usage:
///   w3c_xmlconf_runner <xmlconf-root> [--junit <path>] [--strict]
///
/// Semantics (parshred is a non-validating parser):
///   TYPE="valid"    → must parse without error (well-formed)
///   TYPE="invalid"  → must parse without error (well-formed but DTD-invalid;
///                     a non-validating parser accepts it)
///   TYPE="not-wf"   → must be rejected (not well-formed)
///   TYPE="error"    → skipped (ambiguous per XML spec errata)
///   ENTITIES!="none"→ skipped (external entity resolution not supported)
///
/// Exit code: 0 if no unexpected failures, 1 if any test failed its
/// expectation (unless --strict is given, in which case any skip also fails).

#include <parshred/fast_sax.hpp>
#include <parshred/encoding.hpp>
#include <parshred/mmap_reader.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;
using namespace parshred;

namespace {

/// One enumerated test case from a descriptor file.
struct TestCase {
    std::string id;
    std::string type;        // valid | invalid | not-wf | error
    std::string entities;    // none | general | parameter | both
    std::string uri;         // relative to the descriptor's base dir
    std::string base_dir;    // absolute dir the URI resolves against
    std::string sections;
    std::string description;
    std::string source_file; // descriptor file that declared it
};

/// SAX handler that collects <TEST> elements while tracking xml:base.
class TestEnumerator final : public SaxHandler {
public:
    std::vector<TestCase> cases;
    std::vector<std::string> base_stack;  // xml:base per nesting level
    std::string current_source;
    std::string default_base;  // descriptor file's own dir (relative to root)

    void on_start_element(std::string_view name,
                          const Attribute* attrs, size_t n) override {
        if (name == "TESTCASES") {
            std::string base;
            for (size_t i = 0; i < n; ++i) {
                if (attrs[i].name == "xml:base") base = std::string(attrs[i].value);
            }
            base_stack.push_back(base);
            return;
        }
        if (name != "TEST") return;
        TestCase tc;
        tc.source_file = current_source;
        for (size_t i = 0; i < n; ++i) {
            if (attrs[i].name == "TYPE")      tc.type = std::string(attrs[i].value);
            else if (attrs[i].name == "ENTITIES") tc.entities = std::string(attrs[i].value);
            else if (attrs[i].name == "ID")   tc.id = std::string(attrs[i].value);
            else if (attrs[i].name == "URI")  tc.uri = std::string(attrs[i].value);
            else if (attrs[i].name == "SECTIONS") tc.sections = std::string(attrs[i].value);
        }
        // Base = descriptor's own dir + concatenated xml:base values.
        std::string base = default_base;
        for (auto& b : base_stack) base += b;
        tc.base_dir = base;
        cases.push_back(std::move(tc));
    }

    void on_end_element(std::string_view name) override {
        if (name == "TESTCASES" && !base_stack.empty()) base_stack.pop_back();
    }

    void on_text(std::string_view text) override {
        if (!cases.empty()) {
            auto& d = cases.back().description;
            auto t = std::string(text);
            // collapse whitespace
            bool in_ws = false;
            for (char c : t) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!in_ws && !d.empty()) { d.push_back(' '); in_ws = true; }
                } else {
                    d.push_back(c); in_ws = false;
                }
            }
        }
    }
};

/// Read a file into a string. Returns false on I/O error.
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

/// Try to parse `xml` as well-formed XML. Returns true on success.
bool parses_ok(std::string_view xml) {
    try {
        NullHandler h;
        fast_parse(xml.data(), xml.size(), h);
        return true;
    } catch (...) {
        return false;
    }
}

/// Resolve a test URI against the descriptor base dir + the xmlconf root.
std::string resolve_uri(const std::string& root,
                        const std::string& base,
                        const std::string& uri) {
    fs::path p = fs::path(root) / base / uri;
    return p.generic_string();
}

/// Write a JUnit XML report.
void write_junit(const std::string& path,
                 const std::vector<TestCase>& cases,
                 const std::vector<std::string>& statuses,
                 const std::vector<std::string>& messages) {
    std::ofstream f(path);
    if (!f) return;
    size_t pass = 0, fail = 0, skip = 0;
    for (auto& s : statuses) {
        if (s == "pass") ++pass;
        else if (s == "fail") ++fail;
        else ++skip;
    }
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<testsuites name=\"w3c-xmlconf\" tests=\"" << cases.size()
      << "\" failures=\"" << fail << "\" skipped=\"" << skip
      << "\" passes=\"" << pass << "\">\n";
    f << "<testsuite name=\"W3C XML Conformance Suite\">\n";
    for (size_t i = 0; i < cases.size(); ++i) {
        f << "  <testcase classname=\"w3c." << cases[i].type
          << "\" name=\"" << cases[i].id << "\"";
        if (statuses[i] == "skip") {
            f << "><skipped message=\"" << messages[i] << "\"/></testcase>\n";
        } else if (statuses[i] == "fail") {
            f << "><failure message=\"" << messages[i] << "\"/></testcase>\n";
        } else {
            f << "/>\n";
        }
    }
    f << "</testsuite>\n</testsuites>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string root;
    std::string junit_path;
    bool strict = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--junit" && i + 1 < argc) { junit_path = argv[++i]; continue; }
        if (a == "--strict") { strict = true; continue; }
        if (a.empty() || a[0] == '-') {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
        root = a;
    }
    if (root.empty()) {
        std::fprintf(stderr,
            "usage: w3c_xmlconf_runner <xmlconf-root> [--junit <path>] [--strict]\n"
            "\nDownload the suite from https://www.w3.org/XML/Test/ "
            "(xmlts20080827.zip) and unzip it, then pass the resulting\n"
            "'xmlconf' directory as <xmlconf-root>.\n");
        return 2;
    }
    if (!fs::is_directory(root)) {
        std::fprintf(stderr, "not a directory: %s\n", root.c_str());
        return 2;
    }

    // ── Enumerate test cases from every descriptor file ────────────────
    // Descriptor files are the *.xml files that contain <TESTCASES>/<TEST>.
    // We skip the master xmlconf.xml (it uses DTD external entities that
    // parshred does not resolve) and the eduni/* subtree (XML 1.1 / namespace
    // 1.1 errata tests, which are out of scope for this XML 1.0 runner).
    std::vector<TestCase> all_cases;
    for (auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        if (path.extension() != ".xml") continue;
        auto rel = fs::relative(path, root).generic_string();
        // Skip the master + eduni (XML 1.1 / ns 1.1).
        if (rel == "xmlconf.xml") continue;
        if (rel.rfind("eduni/", 0) == 0) continue;
        // Quick content sniff: only parse descriptor files.
        std::string head;
        if (!read_file(path.string(), head) || head.size() > 4096) {
            // read fully only if small; else re-read below
        }
        if (head.find("<TESTCASES") == std::string::npos &&
            head.find("<TEST ") == std::string::npos) {
            continue;
        }
        std::string content;
        if (!read_file(path.string(), content)) continue;
        TestEnumerator en;
        en.current_source = rel;
        // The descriptor file's own directory is the default base. xml:base
        // attributes (when present) append to this.
        en.default_base = fs::path(rel).parent_path().generic_string();
        try {
            fast_parse(content.data(), content.size(), en);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "warning: descriptor %s failed to parse: %s\n",
                         rel.c_str(), e.what());
            continue;
        }
        for (auto& tc : en.cases) all_cases.push_back(std::move(tc));
    }

    std::fprintf(stderr, "enumerated %zu test cases\n", all_cases.size());

    // ── Run each test ──────────────────────────────────────────────────
    std::vector<std::string> statuses(all_cases.size());
    std::vector<std::string> messages(all_cases.size());
    std::map<std::string, size_t> by_type_pass, by_type_fail, by_type_skip;

    for (size_t i = 0; i < all_cases.size(); ++i) {
        auto& tc = all_cases[i];
        auto& st = statuses[i];
        auto& msg = messages[i];

        if (tc.type == "error") {
            st = "skip"; msg = "TYPE=error (ambiguous per spec)";
            ++by_type_skip[tc.type]; continue;
        }
        if (tc.entities != "none") {
            st = "skip";
            msg = "ENTITIES=" + tc.entities + " (external entities unsupported)";
            ++by_type_skip[tc.type]; continue;
        }
        std::string path = resolve_uri(root, tc.base_dir, tc.uri);
        std::string content;
        if (!read_file(path, content)) {
            st = "skip"; msg = "test file missing: " + path;
            ++by_type_skip[tc.type]; continue;
        }
        // The SAX parser only consumes UTF-8/ASCII directly. Files in other
        // encodings (UTF-16, EUC-JP, etc.) are detected but not transcoded,
        // so skip them with an honest reason rather than conflating
        // "unsupported encoding" with "accepted malformed input".
        auto enc = detect_encoding(content.data(), content.size());
        if (enc != Encoding::UTF8 && enc != Encoding::ASCII &&
            enc != Encoding::ISO_8859_1) {
            st = "skip";
            msg = std::string("encoding ") + encoding_name(enc) +
                  " not transcoded by SAX parser";
            ++by_type_skip[tc.type]; continue;
        }
        bool ok = parses_ok(content);
        bool expect_ok = (tc.type == "valid" || tc.type == "invalid");
        if (ok == expect_ok) {
            st = "pass"; msg = "";
            ++by_type_pass[tc.type];
        } else {
            st = "fail";
            msg = std::string("expected ") + (expect_ok ? "well-formed" : "not-wf")
                  + " but parser " + (ok ? "accepted" : "rejected")
                  + " (" + tc.id + ", " + tc.sections + ")";
            ++by_type_fail[tc.type];
        }
    }

    // ── Summary ────────────────────────────────────────────────────────
    size_t pass = 0, fail = 0, skip = 0;
    for (auto& s : statuses) {
        if (s == "pass") ++pass;
        else if (s == "fail") ++fail;
        else ++skip;
    }
    std::printf("W3C XML Conformance Suite results\n");
    std::printf("==================================\n");
    std::printf("total: %zu   pass: %zu   fail: %zu   skip: %zu\n\n",
                all_cases.size(), pass, fail, skip);
    std::printf("by TYPE (pass/fail/skip):\n");
    for (auto t : {"valid", "invalid", "not-wf", "error"}) {
        std::printf("  %-9s %4zu / %4zu / %4zu\n", t,
                    by_type_pass[t], by_type_fail[t], by_type_skip[t]);
    }
    if (fail > 0) {
        std::printf("\nfirst 20 failures:\n");
        size_t shown = 0;
        for (size_t i = 0; i < all_cases.size() && shown < 20; ++i) {
            if (statuses[i] != "fail") continue;
            std::printf("  [%s] %s — %s\n", all_cases[i].type.c_str(),
                        all_cases[i].id.c_str(), messages[i].c_str());
            ++shown;
        }
    }

    if (!junit_path.empty()) {
        write_junit(junit_path, all_cases, statuses, messages);
        std::fprintf(stderr, "junit report written to %s\n", junit_path.c_str());
    }

    if (fail > 0) return 1;
    if (strict && skip > 0) return 1;
    return 0;
}
