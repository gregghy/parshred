# ── Benchmarks ─────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

add_executable(parshred_bench
    bench_main.cpp
    bench_scanner.cpp
    bench_sax.cpp
    bench_vs_libxml2.cpp
    bench_vs_pugixml.cpp
    bench_vs_expat.cpp
)

target_link_libraries(parshred_bench PRIVATE
    parshred
    benchmark::benchmark
    benchmark::benchmark_main
)

# Link comparison parsers (all available on the system)
pkg_check_modules(LIBXML2 IMPORTED_TARGET libxml-2.0)
if(LIBXML2_FOUND)
    target_link_libraries(parshred_bench PRIVATE PkgConfig::LIBXML2)
    target_compile_definitions(parshred_bench PRIVATE HAS_LIBXML2=1)
endif()

pkg_check_modules(EXPAT IMPORTED_TARGET expat)
if(EXPAT_FOUND)
    target_link_libraries(parshred_bench PRIVATE PkgConfig::EXPAT)
    target_compile_definitions(parshred_bench PRIVATE HAS_EXPAT=1)
endif()

pkg_check_modules(PUGIXML IMPORTED_TARGET pugixml)
if(PUGIXML_FOUND)
    target_link_libraries(parshred_bench PRIVATE PkgConfig::PUGIXML)
    target_compile_definitions(parshred_bench PRIVATE HAS_PUGIXML=1)
endif()

# Generate benchmark data at build time
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/data/small.xml
           ${CMAKE_CURRENT_BINARY_DIR}/data/medium.xml
           ${CMAKE_CURRENT_BINARY_DIR}/data/large.xml
           ${CMAKE_CURRENT_BINARY_DIR}/data/deep.xml
           ${CMAKE_CURRENT_BINARY_DIR}/data/attrs.xml
           ${CMAKE_CURRENT_BINARY_DIR}/data/text.xml
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/data
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/data/generate_xml.py
            ${CMAKE_CURRENT_BINARY_DIR}/data
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/generate_xml.py
    COMMENT "Generating benchmark XML data..."
)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(bench_data ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/data/small.xml
    )
    add_dependencies(parshred_bench bench_data)
endif()

target_compile_definitions(parshred_bench PRIVATE
    BENCH_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/data/"
)
