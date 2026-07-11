cmake_minimum_required(VERSION 3.20)

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_parshred bindings.cpp)
target_link_libraries(_parshred PRIVATE parshred)
