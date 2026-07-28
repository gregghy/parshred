/// @file parshred_py.cpp
/// @brief Python bindings for parshred using pybind11.
///
/// Provides:
///   - parshred.parse(xml_str) → Document (FastDom wrapper)
///   - parshred.parse_file(path) → Document
///   - Document.root → Element
///   - Element.name, .text, .attrs, .children
///   - Element.xpath(expr) → list
///   - parshred.validate(xml_str) → list of issues
///   - parshred.DomBuilder for programmatic construction

#ifdef PARSHRED_PYTHON_BINDINGS

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <parshred/dom_fast.hpp>
#include <parshred/xpath.hpp>
#include <parshred/writer.hpp>
#include <parshred/namespace.hpp>
#include <parshred/dtd.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

/// Python-friendly wrapper around FastDom.
/// Holds the input string to keep string_views valid.
struct PyDocument {
    std::string source;
    parshred::FastDom dom;

    PyDocument(std::string src) : source(std::move(src)) {
        dom = parshred::fast_dom_parse<0>(source.data(), source.size());
    }
};

/// Python-friendly element accessor.
struct PyElement {
    const parshred::FastDom* dom;
    uint32_t idx;

    [[nodiscard]] std::string name() const {
        return std::string(dom->name(dom->nodes[idx]));
    }

    [[nodiscard]] std::string text() const {
        return parshred::xpath::get_text_content(*dom, idx);
    }

    [[nodiscard]] py::dict attrs() const {
        py::dict result;
        uint32_t attr = dom->nodes[idx].first_attr;
        while (attr) {
            auto& a = dom->nodes[attr];
            result[py::cast(std::string(dom->name(a)))] = py::cast(std::string(dom->value(a)));
            attr = a.next_sibling;
        }
        return result;
    }

    [[nodiscard]] std::string attr(const std::string& name) const {
        return std::string(dom->attr(dom->nodes[idx], name));
    }

    [[nodiscard]] std::vector<PyElement> children() const {
        std::vector<PyElement> result;
        uint32_t child = dom->nodes[idx].first_child;
        while (child) {
            if (dom->nodes[child].type == 1) {
                result.push_back({dom, child});
            }
            child = dom->nodes[child].next_sibling;
        }
        return result;
    }

    [[nodiscard]] std::vector<PyElement> xpath(const std::string& expr) const {
        // Evaluate from this element as context
        parshred::xpath::NodeSet context = {idx};
        auto parsed = parshred::xpath::parse_xpath(expr);
        for (const auto& step : parsed.steps) {
            context = parshred::xpath::eval_step(*dom, context, step);
            if (context.empty()) break;
        }
        std::vector<PyElement> result;
        for (uint32_t i : context) {
            result.push_back({dom, i});
        }
        return result;
    }

    [[nodiscard]] std::string to_xml(bool pretty = true) const {
        parshred::WriteOptions opts;
        opts.pretty = pretty;
        opts.xml_declaration = false;
        parshred::XmlWriter writer(opts);
        return writer.serialize_node_str(*dom, idx);
    }
};

} // anonymous namespace

PYBIND11_MODULE(_parshred, m) {
    m.doc() = "Parshred: High-performance XML parser for Python";

    // ── Document ─────────────────────────────────────────────────────
    py::class_<PyDocument>(m, "Document")
        .def(py::init<std::string>(), py::arg("xml"))
        .def_property_readonly("root", [](const PyDocument& doc) {
            return PyElement{&doc.dom, doc.dom.root_idx};
        })
        .def("xpath", [](const PyDocument& doc, const std::string& expr) {
            auto nodes = parshred::xpath::evaluate(doc.dom, expr);
            std::vector<PyElement> result;
            for (uint32_t idx : nodes) {
                result.push_back({&doc.dom, idx});
            }
            return result;
        })
        .def("xpath_string", [](const PyDocument& doc, const std::string& expr) {
            return parshred::xpath::evaluate_string(doc.dom, expr);
        })
        .def("to_xml", [](const PyDocument& doc, bool pretty) {
            parshred::WriteOptions opts;
            opts.pretty = pretty;
            parshred::XmlWriter writer(opts);
            return writer.serialize(doc.dom);
        }, py::arg("pretty") = true)
        .def_property_readonly("element_count", [](const PyDocument& doc) {
            return doc.dom.element_count();
        });

    // ── Element ──────────────────────────────────────────────────────
    py::class_<PyElement>(m, "Element")
        .def_property_readonly("name", &PyElement::name)
        .def_property_readonly("text", &PyElement::text)
        .def_property_readonly("attrs", &PyElement::attrs)
        .def_property_readonly("children", &PyElement::children)
        .def("attr", &PyElement::attr)
        .def("xpath", &PyElement::xpath)
        .def("to_xml", &PyElement::to_xml, py::arg("pretty") = true)
        .def("__repr__", [](const PyElement& e) {
            return "<Element '" + e.name() + "'>";
        });

    // ── Module-level functions ───────────────────────────────────────
    m.def("parse", [](const std::string& xml) {
        return std::make_unique<PyDocument>(xml);
    }, py::arg("xml"), "Parse an XML string into a Document.");

    m.def("parse_file", [](const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open file: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return std::make_unique<PyDocument>(ss.str());
    }, py::arg("path"), "Parse an XML file into a Document.");

    m.def("validate", [](const std::string& xml) {
        return parshred::check_wellformedness(xml);
    }, py::arg("xml"), "Check well-formedness of an XML string.");
}

#endif // PARSHRED_PYTHON_BINDINGS
