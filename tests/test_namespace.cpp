/// @file test_namespace.cpp
/// @brief Unit tests for XML Namespace support.

#include <parshred/namespace.hpp>
#include <parshred/fast_sax.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace parshred;

// ── NsContext Basic ──────────────────────────────────────────────────

TEST(Namespace, DefaultXmlBinding) {
    NsContext ctx;
    // xml: prefix is always bound
    EXPECT_EQ(ctx.resolve("xml"), ns::XML);
}

TEST(Namespace, DeclareAndResolve) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("soap", "http://schemas.xmlsoap.org/soap/envelope/");
    EXPECT_EQ(ctx.resolve("soap"), "http://schemas.xmlsoap.org/soap/envelope/");
    EXPECT_EQ(ctx.resolve("unknown"), "");
}

TEST(Namespace, DefaultNamespace) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("", "http://example.com/default");
    EXPECT_EQ(ctx.resolve(""), "http://example.com/default");
}

TEST(Namespace, ScopeIsolation) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("a", "http://a.com");
    
    ctx.push_scope();
    ctx.declare("b", "http://b.com");
    EXPECT_EQ(ctx.resolve("a"), "http://a.com");
    EXPECT_EQ(ctx.resolve("b"), "http://b.com");
    
    ctx.pop_scope();
    EXPECT_EQ(ctx.resolve("a"), "http://a.com");
    EXPECT_EQ(ctx.resolve("b"), "");  // b no longer in scope
}

TEST(Namespace, Shadowing) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("ns", "http://old.com");
    
    ctx.push_scope();
    ctx.declare("ns", "http://new.com");
    EXPECT_EQ(ctx.resolve("ns"), "http://new.com");
    
    ctx.pop_scope();
    EXPECT_EQ(ctx.resolve("ns"), "http://old.com");
}

// ── QName Resolution ─────────────────────────────────────────────────

TEST(Namespace, ResolveQName) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("soap", "http://schemas.xmlsoap.org/soap/envelope/");
    
    auto qn = ctx.resolve_name("soap:Envelope");
    EXPECT_EQ(qn.prefix, "soap");
    EXPECT_EQ(qn.local_name, "Envelope");
    EXPECT_EQ(qn.namespace_uri, "http://schemas.xmlsoap.org/soap/envelope/");
}

TEST(Namespace, ResolveNoPrefix) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("", "http://default.com");
    
    auto qn = ctx.resolve_name("element");
    EXPECT_EQ(qn.prefix, "");
    EXPECT_EQ(qn.local_name, "element");
    EXPECT_EQ(qn.namespace_uri, "http://default.com");
}

TEST(Namespace, ResolveUnknownPrefix) {
    NsContext ctx;
    ctx.push_scope();
    
    auto qn = ctx.resolve_name("foo:bar");
    EXPECT_EQ(qn.prefix, "foo");
    EXPECT_EQ(qn.local_name, "bar");
    EXPECT_EQ(qn.namespace_uri, "");
}

// ── Integration with SAX ─────────────────────────────────────────────

TEST(Namespace, ProcessElementNs) {
    NsContext ctx;
    
    Attribute attrs[] = {
        {"xmlns:soap", "http://schemas.xmlsoap.org/soap/envelope/"},
        {"xmlns", "http://example.com"},
    };
    
    auto qn = process_element_ns("soap:Envelope", attrs, 2, ctx);
    EXPECT_EQ(qn.prefix, "soap");
    EXPECT_EQ(qn.local_name, "Envelope");
    EXPECT_EQ(qn.namespace_uri, "http://schemas.xmlsoap.org/soap/envelope/");
    
    // Default namespace should be set
    auto qn2 = ctx.resolve_name("child");
    EXPECT_EQ(qn2.namespace_uri, "http://example.com");
    
    // Pop scope
    ctx.pop_scope();
    EXPECT_EQ(ctx.resolve("soap"), "");
    EXPECT_EQ(ctx.resolve(""), "");
}

TEST(Namespace, FullSaxParsing) {
    // Parse a namespace-rich document using SAX + NsContext
    struct NsHandler final : SaxHandler {
        NsContext ns;
        std::vector<QName> elements;
        
        void on_start_element(std::string_view name,
                              const Attribute* attrs, size_t nattrs) override {
            auto qn = process_element_ns(name, attrs, nattrs, ns);
            elements.push_back(qn);
        }
        void on_end_element(std::string_view) override {
            ns.pop_scope();
        }
    };
    
    std::string xml = R"(
        <root xmlns="http://default.com" xmlns:x="http://x.com">
            <child/>
            <x:special/>
        </root>
    )";
    
    NsHandler handler;
    fast_parse(xml, handler);
    
    ASSERT_EQ(handler.elements.size(), 3u);
    
    // root: default namespace
    EXPECT_EQ(handler.elements[0].local_name, "root");
    EXPECT_EQ(handler.elements[0].namespace_uri, "http://default.com");
    
    // child: inherits default namespace
    EXPECT_EQ(handler.elements[1].local_name, "child");
    EXPECT_EQ(handler.elements[1].namespace_uri, "http://default.com");
    
    // x:special: prefixed namespace
    EXPECT_EQ(handler.elements[2].local_name, "special");
    EXPECT_EQ(handler.elements[2].prefix, "x");
    EXPECT_EQ(handler.elements[2].namespace_uri, "http://x.com");
}

TEST(Namespace, NestedScopes) {
    struct NsHandler final : SaxHandler {
        NsContext ns;
        std::vector<std::pair<std::string, std::string>> resolved;
        
        void on_start_element(std::string_view name,
                              const Attribute* attrs, size_t nattrs) override {
            auto qn = process_element_ns(name, attrs, nattrs, ns);
            resolved.emplace_back(std::string(qn.local_name),
                                  std::string(qn.namespace_uri));
        }
        void on_end_element(std::string_view) override {
            ns.pop_scope();
        }
    };
    
    std::string xml = R"(
        <a xmlns:ns="http://outer.com">
            <ns:b>
                <c xmlns:ns="http://inner.com">
                    <ns:d/>
                </c>
            </ns:b>
        </a>
    )";
    
    NsHandler handler;
    fast_parse(xml, handler);
    
    ASSERT_EQ(handler.resolved.size(), 4u);
    EXPECT_EQ(handler.resolved[0].first, "a");      // no prefix
    EXPECT_EQ(handler.resolved[1].first, "b");      // ns:b → outer
    EXPECT_EQ(handler.resolved[1].second, "http://outer.com");
    EXPECT_EQ(handler.resolved[2].first, "c");      // redefines ns
    EXPECT_EQ(handler.resolved[3].first, "d");      // ns:d → inner (shadowed)
    EXPECT_EQ(handler.resolved[3].second, "http://inner.com");
}

// ── Depth tracking ───────────────────────────────────────────────────

TEST(Namespace, DepthTracking) {
    NsContext ctx;
    EXPECT_EQ(ctx.depth(), 0u);
    ctx.push_scope();
    EXPECT_EQ(ctx.depth(), 1u);
    ctx.push_scope();
    EXPECT_EQ(ctx.depth(), 2u);
    ctx.pop_scope();
    EXPECT_EQ(ctx.depth(), 1u);
    ctx.pop_scope();
    EXPECT_EQ(ctx.depth(), 0u);
}

TEST(Namespace, Reset) {
    NsContext ctx;
    ctx.push_scope();
    ctx.declare("foo", "http://foo.com");
    ctx.reset();
    EXPECT_EQ(ctx.resolve("foo"), "");
    EXPECT_EQ(ctx.resolve("xml"), ns::XML);
    EXPECT_EQ(ctx.depth(), 0u);
}
