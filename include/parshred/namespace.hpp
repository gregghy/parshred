#pragma once
/// @file namespace.hpp
/// @brief XML Namespace support for parshred.
///
/// Implements XML Namespaces 1.0:
///   - xmlns:prefix="uri" declarations
///   - Default namespace (xmlns="uri")
///   - Prefix resolution for elements and attributes
///   - Namespace stack (push on start-element, pop on end-element)
///
/// Design goals:
///   - Zero overhead when namespaces are not used
///   - O(1) prefix lookup for common cases (< 8 prefixes per scope)
///   - Minimal allocations (arena-based string storage)

#include <parshred/common.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace parshred {

/// A resolved namespace-qualified name.
struct QName {
    std::string_view local_name;   // Local part (after ':')
    std::string_view prefix;       // Prefix (before ':'), empty for default ns
    std::string_view namespace_uri; // Resolved URI
};

/// Well-known namespace URIs.
namespace ns {
    inline constexpr std::string_view XML = "http://www.w3.org/XML/1998/namespace";
    inline constexpr std::string_view XMLNS = "http://www.w3.org/2000/xmlns/";
} // namespace ns

/// A namespace binding: prefix → URI.
struct NsBinding {
    std::string_view prefix;
    std::string_view uri;
};

/// Namespace context — tracks active namespace bindings with scope management.
///
/// Usage:
///   NsContext ctx;
///   ctx.push_scope();
///   ctx.declare("soap", "http://schemas.xmlsoap.org/soap/envelope/");
///   ctx.declare("", "http://example.com/default");  // default namespace
///   auto uri = ctx.resolve("soap");  // → "http://..."
///   auto uri2 = ctx.resolve("");     // → "http://example.com/default"
///   ctx.pop_scope();
///   // "soap" and default are no longer bound
class NsContext {
public:
    NsContext() {
        // Pre-declare the xml: prefix (always in scope per spec)
        bindings_.push_back({"xml", ns::XML});
        scope_starts_.push_back(0);
    }

    /// Push a new scope (called at start-element).
    void push_scope() noexcept {
        scope_starts_.push_back(bindings_.size());
    }

    /// Pop a scope (called at end-element).
    /// Removes all bindings declared in the current scope.
    void pop_scope() noexcept {
        if (scope_starts_.size() > 1) {
            bindings_.resize(scope_starts_.back());
            scope_starts_.pop_back();
        }
    }

    /// Declare a namespace binding in the current scope.
    /// @param prefix  Empty string for default namespace.
    /// @param uri     The namespace URI.
    void declare(std::string_view prefix, std::string_view uri) {
        bindings_.push_back({prefix, uri});
    }

    /// Resolve a prefix to its namespace URI.
    /// Returns empty string_view if the prefix is not bound.
    /// Search is from most recent to oldest (inner scope shadows outer).
    [[nodiscard]] std::string_view resolve(std::string_view prefix) const noexcept {
        // Search in reverse for most recent binding
        for (size_t i = bindings_.size(); i > 0; --i) {
            if (bindings_[i - 1].prefix == prefix) {
                return bindings_[i - 1].uri;
            }
        }
        return {};
    }

    /// Resolve a qualified name (e.g., "soap:Envelope") into its components.
    /// @param qname  The full qualified name (may contain ':').
    /// @return QName with resolved namespace URI.
    [[nodiscard]] QName resolve_name(std::string_view qname) const noexcept {
        auto colon = qname.find(':');
        if (colon == std::string_view::npos) {
            // No prefix — use default namespace
            return {qname, {}, resolve({})};
        }
        std::string_view prefix = qname.substr(0, colon);
        std::string_view local = qname.substr(colon + 1);
        return {local, prefix, resolve(prefix)};
    }

    /// Parse namespace declarations from attributes.
    /// Processes xmlns:prefix="uri" and xmlns="uri" attributes.
    /// @param attrs    Array of attribute name/value pairs.
    /// @param nattrs   Number of attributes.
    /// @return Number of namespace declarations found.
    size_t process_declarations(const std::pair<std::string_view, std::string_view>* attrs,
                                size_t nattrs) {
        size_t count = 0;
        for (size_t i = 0; i < nattrs; ++i) {
            auto name = attrs[i].first;
            auto value = attrs[i].second;
            if (name == "xmlns") {
                declare({}, value);
                ++count;
            } else if (name.size() > 6 && name.substr(0, 6) == "xmlns:") {
                declare(name.substr(6), value);
                ++count;
            }
        }
        return count;
    }

    /// Get all active bindings in the current scope.
    [[nodiscard]] const std::vector<NsBinding>& bindings() const noexcept {
        return bindings_;
    }

    /// Current scope depth (0 = top level).
    [[nodiscard]] size_t depth() const noexcept {
        return scope_starts_.size() - 1;
    }

    /// Reset all state.
    void reset() {
        bindings_.clear();
        scope_starts_.clear();
        bindings_.push_back({"xml", ns::XML});
        scope_starts_.push_back(0);
    }

private:
    std::vector<NsBinding> bindings_;
    std::vector<size_t>    scope_starts_;
};

/// Extract namespace declarations from a raw attribute list.
/// This is a helper for integration with SaxHandler.
///
/// @param name       Element name (may have prefix like "soap:Envelope").
/// @param attrs      Attribute array from SAX callback.
/// @param nattrs     Number of attributes.
/// @param ctx        Namespace context to update.
/// @return QName with resolved namespace for the element.
inline QName process_element_ns(std::string_view name,
                                const struct Attribute* attrs, size_t nattrs,
                                NsContext& ctx) {
    ctx.push_scope();
    
    // First pass: collect namespace declarations
    for (size_t i = 0; i < nattrs; ++i) {
        auto attr_name = attrs[i].name;
        auto attr_val = attrs[i].value;
        if (attr_name == "xmlns") {
            ctx.declare({}, attr_val);
        } else if (attr_name.size() > 6 && attr_name.substr(0, 6) == "xmlns:") {
            ctx.declare(attr_name.substr(6), attr_val);
        }
    }
    
    // Resolve the element name
    return ctx.resolve_name(name);
}

/// Namespace-aware SAX handler mixin.
/// Extend your SaxHandler with this to get namespace resolution.
///
/// Usage:
/// @code
///   struct MyNsHandler : parshred::SaxHandler {
///       parshred::NsContext ns_ctx;
///       void on_start_element(std::string_view name,
///                             const Attribute* attrs, size_t nattrs) override {
///           auto qn = parshred::process_element_ns(name, attrs, nattrs, ns_ctx);
///           // qn.namespace_uri, qn.local_name, qn.prefix are resolved
///       }
///       void on_end_element(std::string_view name) override {
///           ns_ctx.pop_scope();
///       }
///   };
/// @endcode

} // namespace parshred
