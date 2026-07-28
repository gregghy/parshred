# ── Tests ──────────────────────────────────────────────────────────────
add_executable(parshred_tests
    test_mmap.cpp
    test_platform.cpp
    test_scanner.cpp
    test_tokenizer.cpp
    test_sax.cpp
    test_security.cpp
    test_fast_sax.cpp
    test_pipeline.cpp
    test_dom.cpp
    test_namespace.cpp
    test_xpath.cpp
    test_dtd.cpp
    test_writer.cpp
    test_encoding.cpp
    test_xsd.cpp
    test_error.cpp
    test_pull_parser.cpp
    test_normalization.cpp
    test_conformance.cpp
    test_xpath_extended.cpp
    test_realworld.cpp
)

target_link_libraries(parshred_tests PRIVATE
    parshred
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(parshred_tests)

# ── W3C XML Conformance Suite runner ──────────────────────────────────
# Standalone executable (not a gtest). Downloads the official W3C suite in
# CI and runs it; emits a JUnit XML report. See .github/workflows/w3c.yml.
add_executable(w3c_xmlconf_runner w3c_xmlconf_runner.cpp)
target_link_libraries(w3c_xmlconf_runner PRIVATE parshred)
# Not registered with ctest — it needs the external suite, which CI fetches.
