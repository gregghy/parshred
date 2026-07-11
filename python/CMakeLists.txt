# Python bindings for parshred using pybind11.
#
# Build:
#   cmake -B build-python -DBUILD_PYTHON=ON
#   cmake --build build-python
#
# Or using pip:
#   pip install .  (from project root with setup.py)

find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(parshred_py parshred_py.cpp)
    target_compile_definitions(parshred_py PRIVATE PARSHRED_PYTHON_BINDINGS)
    target_include_directories(parshred_py PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(parshred_py PRIVATE -O3 -march=native)
    set_target_properties(parshred_py PROPERTIES
        OUTPUT_NAME "parshred"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    )
    message(STATUS "Python bindings: ENABLED (pybind11 found)")
else()
    message(STATUS "Python bindings: DISABLED (pybind11 not found)")
endif()
