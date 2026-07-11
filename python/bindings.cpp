/// @file bindings.cpp
/// @brief Python bindings for parshred via pybind11.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include <parshred/parshred.hpp>

#include <string>
#include <vector>
#include <tuple>

namespace py = pybind11;
using namespace parshred;

/// An event from iterparse: (event_type, name_or_text, attributes_dict)
using ParseEvent = std::tuple<std::string, std::string, py::dict>;

/// Collect all SAX events into a list of tuples.
static std::vector<ParseEvent> iterparse_file(const std::string& path) {
    std::vector<ParseEvent> events;

    SaxParser parser;

    parser.on_start_element([&](std::string_view name, std::span<const Attribute> attrs) {
        py::dict d;
        for (const auto& a : attrs) {
            d[py::str(std::string(a.name))] = py::str(std::string(a.value));
        }
        events.emplace_back("start", std::string(name), std::move(d));
    });

    parser.on_end_element([&](std::string_view name) {
        events.emplace_back("end", std::string(name), py::dict());
    });

    parser.on_text([&](std::string_view text) {
        events.emplace_back("text", std::string(text), py::dict());
    });

    parser.on_comment([&](std::string_view text) {
        events.emplace_back("comment", std::string(text), py::dict());
    });

    parser.on_cdata([&](std::string_view text) {
        events.emplace_back("cdata", std::string(text), py::dict());
    });

    parser.parse_file(path);
    return events;
}

static std::vector<ParseEvent> iterparse_string(const std::string& xml) {
    std::vector<ParseEvent> events;

    SaxParser parser;

    parser.on_start_element([&](std::string_view name, std::span<const Attribute> attrs) {
        py::dict d;
        for (const auto& a : attrs) {
            d[py::str(std::string(a.name))] = py::str(std::string(a.value));
        }
        events.emplace_back("start", std::string(name), std::move(d));
    });

    parser.on_end_element([&](std::string_view name) {
        events.emplace_back("end", std::string(name), py::dict());
    });

    parser.on_text([&](std::string_view text) {
        events.emplace_back("text", std::string(text), py::dict());
    });

    parser.parse_string(xml);
    return events;
}

PYBIND11_MODULE(_parshred, m) {
    m.doc() = "parshred — The World's Fastest XML Parser";

    // Expose version
    m.attr("__version__") = "0.1.0";
    m.attr("VERSION_MAJOR") = VERSION_MAJOR;
    m.attr("VERSION_MINOR") = VERSION_MINOR;
    m.attr("VERSION_PATCH") = VERSION_PATCH;

    // SaxParser class
    py::class_<SaxParser>(m, "SaxParser")
        .def(py::init<>())
        .def("on_start_element", [](SaxParser& self, py::function cb) {
            self.on_start_element([cb](std::string_view name, std::span<const Attribute> attrs) {
                py::dict d;
                for (const auto& a : attrs) {
                    d[py::str(std::string(a.name))] = py::str(std::string(a.value));
                }
                cb(py::str(std::string(name)), d);
            });
        })
        .def("on_end_element", [](SaxParser& self, py::function cb) {
            self.on_end_element([cb](std::string_view name) {
                cb(py::str(std::string(name)));
            });
        })
        .def("on_text", [](SaxParser& self, py::function cb) {
            self.on_text([cb](std::string_view text) {
                cb(py::str(std::string(text)));
            });
        })
        .def("on_comment", [](SaxParser& self, py::function cb) {
            self.on_comment([cb](std::string_view text) {
                cb(py::str(std::string(text)));
            });
        })
        .def("on_cdata", [](SaxParser& self, py::function cb) {
            self.on_cdata([cb](std::string_view text) {
                cb(py::str(std::string(text)));
            });
        })
        .def("parse_file", &SaxParser::parse_file)
        .def("parse_string", &SaxParser::parse_string)
        .def("set_max_depth", &SaxParser::set_max_depth)
        .def("set_max_entity_expansions", &SaxParser::set_max_entity_expansions)
        .def_property_readonly("stats", [](const SaxParser& self) {
            const auto& s = self.stats();
            py::dict d;
            d["elements"] = s.elements;
            d["attributes"] = s.attributes;
            d["text_nodes"] = s.text_nodes;
            d["comments"] = s.comments;
            d["cdata_nodes"] = s.cdata_nodes;
            d["bytes_parsed"] = s.bytes_parsed;
            return d;
        });

    // Convenience functions
    m.def("iterparse", &iterparse_file,
          py::arg("path"),
          "Parse an XML file and return a list of (event_type, name, attrs) tuples.");

    m.def("iterparse_string", &iterparse_string,
          py::arg("xml"),
          "Parse an XML string and return a list of (event_type, name, attrs) tuples.");

    m.def("load", &iterparse_file,
          py::arg("path"),
          "Load an XML file (alias for iterparse).");

    // Exceptions
    py::register_exception<ParseError>(m, "ParseError");
    py::register_exception<IOError>(m, "IOError");
    py::register_exception<SecurityError>(m, "SecurityError");
}
