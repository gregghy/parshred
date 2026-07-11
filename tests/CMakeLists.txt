# ── Tests ──────────────────────────────────────────────────────────────
add_executable(parshred_tests
    test_mmap.cpp
    test_scanner.cpp
    test_tokenizer.cpp
    test_sax.cpp
    test_security.cpp
    test_fast_sax.cpp
    test_pipeline.cpp
)

target_link_libraries(parshred_tests PRIVATE
    parshred
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(parshred_tests)
