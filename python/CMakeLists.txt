# Python bindings for parshred using pybind11.
#
# When built via scikit-build-core (pip install .):
#   The module is installed as parshred/_parshred.cpython-*.so
#   alongside parshred/__init__.py
#
# When built standalone (cmake -DPARSHRED_BUILD_PYTHON=ON):
#   The module lands in ${CMAKE_BINARY_DIR}/python/

find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(_parshred parshred_py.cpp)
    target_compile_definitions(_parshred PRIVATE PARSHRED_PYTHON_BINDINGS)
    target_link_libraries(_parshred PRIVATE parshred)
    # NOTE: do NOT use -march=native here — it produces wheels that crash on
    # CPUs lacking the build host's SIMD features. The core library already
    # does runtime SIMD dispatch (separate sse42/avx2/avx512 object libs), so
    # the binding shim only needs portable -O3. x86-64-v2 baseline guarantees
    # SSE4.1/SSE4.2/POPCNT/CMPXCHG16B, which is the floor on every x86-64 CPU
    # we support (the README lists SSE4.2 as the minimum).
    target_compile_options(_parshred PRIVATE -O3 -march=x86-64-v2)

    # When building via scikit-build-core, install into the Python package
    install(TARGETS _parshred DESTINATION parshred)
    install(FILES ${CMAKE_SOURCE_DIR}/python/parshred/__init__.py DESTINATION parshred)

    # For standalone builds, also output to build/python for convenience
    set_target_properties(_parshred PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/parshred"
    )

    # Copy __init__.py to build dir for standalone use
    add_custom_command(TARGET _parshred POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/python/parshred/__init__.py
            ${CMAKE_BINARY_DIR}/python/parshred/__init__.py
    )

    message(STATUS "Python bindings: ENABLED (pybind11 found)")
else()
    message(STATUS "Python bindings: DISABLED (pybind11 not found)")
endif()
