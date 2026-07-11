/// @file test_fast_sax.cpp
/// @brief Unit tests for the fused single-pass FastSaxParser.

#include <parshred/fast_sax.hpp>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace parshred;

// ── Test handler that records events ─────────────────────────────────

struct RecordingHandler : SaxHandler {
    struct Event {
        enum Type { Start, End, Text, Comment, CData, PI, XmlDecl } type;
        std::string name;
        std::string text;
        std::vector<std::pair<std::string, std::string>> attrs;
    };
    std::vector<Event> events;

    void on_start_element(std::string_view name,
                          const Attribute* attrs, size_t nattrs) override {
        Event e{Event::Start, std::string(name), {}, {}};
        for (size_t i = 0; i < nattrs; ++i) {
            e.attrs.emplace_back(std::string(attrs[i].name),
                                 std::string(attrs[i].value));
        }
        events.push_back(std::move(e));
    }
    void on_end_element(std::string_view name) override {
        events.push_back({Event::End, std::string(name), {}, {}});
    }
    void on_text(std::string_view text) override {
        events.push_back({Event::Text, {}, std::string(text), {}});
    }
    void on_comment(std::string_view text) override {
        events.push_back({Event::Comment, {}, std::string(text), {}});
    }
    void on_cdata(std::string_view text) override {
        events.push_back({Event::CData, {}, std::string(text), {}});
    }
    void on_processing_instruction(std::string_view target,
                                   std::string_view data) override {
        events.push_back({Event::PI, std::string(target), std::string(data), {}});
    }
    void on_xml_declaration(std::string_view version,
                            std::string_view encoding,
                            std::string_view standalone) override {
        events.push_back({Event::XmlDecl, std::string(version),
                          std::string(encoding), {}});
    }
};

// ── Basic tests ──────────────────────────────────────────────────────

TEST(FastSax, SimpleElement) {
    std::string xml = "<root>hello</root>";
    RecordingHandler h;
    fast_parse(xml, h);

    ASSERT_EQ(h.events.size(), 3u);
    EXPECT_EQ(h.events[0].type, RecordingHandler::Event::Start);
    EXPECT_EQ(h.events[0].name, "root");
    EXPECT_EQ(h.events[1].type, RecordingHandler::Event::Text);
    EXPECT_EQ(h.events[1].text, "hello");
    EXPECT_EQ(h.events[2].type, RecordingHandler::Event::End);
    EXPECT_EQ(h.events[2].name, "root");
}

TEST(FastSax, SelfClosingElement) {
    std::string xml = "<br/>";
    RecordingHandler h;
    fast_parse(xml, h);

    ASSERT_EQ(h.events.size(), 2u);
    EXPECT_EQ(h.events[0].type, RecordingHandler::Event::Start);
    EXPECT_EQ(h.events[0].name, "br");
    EXPECT_EQ(h.events[1].type, RecordingHandler::Event::End);
    EXPECT_EQ(h.events[1].name, "br");
}

TEST(FastSax, Attributes) {
    std::string xml = R"(<tag a="1" b='2'/>)";
    RecordingHandler h;
    fast_parse(xml, h);

    ASSERT_GE(h.events.size(), 1u);
    ASSERT_EQ(h.events[0].attrs.size(), 2u);
    EXPECT_EQ(h.events[0].attrs[0].first, "a");
    EXPECT_EQ(h.events[0].attrs[0].second, "1");
    EXPECT_EQ(h.events[0].attrs[1].first, "b");
    EXPECT_EQ(h.events[0].attrs[1].second, "2");
}

TEST(FastSax, NestedElements) {
    std::string xml = "<a><b><c>text</c></b></a>";
    RecordingHandler h;
    fast_parse(xml, h);

    // start a, start b, start c, text, end c, end b, end a
    ASSERT_EQ(h.events.size(), 7u);
    EXPECT_EQ(h.events[0].name, "a");
    EXPECT_EQ(h.events[1].name, "b");
    EXPECT_EQ(h.events[2].name, "c");
    EXPECT_EQ(h.events[3].text, "text");
    EXPECT_EQ(h.events[4].name, "c");
    EXPECT_EQ(h.events[5].name, "b");
    EXPECT_EQ(h.events[6].name, "a");
}

TEST(FastSax, Comment) {
    std::string xml = "<root><!-- a comment --></root>";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_comment = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Comment) {
            EXPECT_EQ(e.text, " a comment ");
            found_comment = true;
        }
    }
    EXPECT_TRUE(found_comment);
}

TEST(FastSax, CData) {
    std::string xml = "<root><![CDATA[raw <data> & stuff]]></root>";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_cdata = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::CData) {
            EXPECT_EQ(e.text, "raw <data> & stuff");
            found_cdata = true;
        }
    }
    EXPECT_TRUE(found_cdata);
}

TEST(FastSax, ProcessingInstruction) {
    std::string xml = "<?mypi some data?><root/>";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_pi = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::PI) {
            EXPECT_EQ(e.name, "mypi");
            EXPECT_EQ(e.text, "some data");
            found_pi = true;
        }
    }
    EXPECT_TRUE(found_pi);
}

TEST(FastSax, XmlDeclaration) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?><root/>)";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_decl = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::XmlDecl) {
            EXPECT_EQ(e.name, "1.0");     // version
            EXPECT_EQ(e.text, "UTF-8");   // encoding
            found_decl = true;
        }
    }
    EXPECT_TRUE(found_decl);
}

TEST(FastSax, EntityExpansionNormal) {
    std::string xml = "<root>&lt;&gt;&amp;&apos;&quot;</root>";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_text = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Text) {
            EXPECT_EQ(e.text, "<>&'\"");
            found_text = true;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(FastSax, TurboModeSkipsEntities) {
    std::string xml = "<root>&lt;</root>";
    RecordingHandler h;
    fast_parse_turbo(xml, h);

    // In turbo mode, text stops at '<' — the '&' is inside text
    // and not treated as entity. The text callback gets raw bytes.
    bool found_text = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Text) {
            // Should get "&lt;" as raw text (no expansion)
            EXPECT_TRUE(e.text.find('&') != std::string::npos);
            found_text = true;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(FastSax, CountingHandler) {
    std::string xml = R"(<root><a x="1"/><b>text</b><c/></root>)";
    CountingHandler h;
    fast_parse(xml, h);

    EXPECT_EQ(h.elements, 4u);  // root, a, b, c
    EXPECT_EQ(h.end_tags, 4u);  // all close (a, c are self-closing -> also fire end)
    EXPECT_EQ(h.attributes, 1u);
    EXPECT_GE(h.text_nodes, 1u);
}

TEST(FastSax, NullHandler) {
    std::string xml = "<root><a/><b>text</b></root>";
    NullHandler h;
    // Just verify it doesn't crash
    EXPECT_NO_THROW(fast_parse(xml, h));
    EXPECT_NO_THROW(fast_parse_turbo(xml, h));
}

// ── Security tests ───────────────────────────────────────────────────

TEST(FastSax, MaxDepthExceeded) {
    std::string xml;
    for (int i = 0; i < 600; ++i) xml += "<a>";
    for (int i = 0; i < 600; ++i) xml += "</a>";

    FastSaxParser<ParseMode::Normal> parser;
    parser.set_max_depth(512);
    NullHandler h;
    EXPECT_THROW(parser.parse(xml.data(), xml.size(), h), SecurityError);
}

TEST(FastSax, MaxDepthOk) {
    std::string xml;
    for (int i = 0; i < 100; ++i) xml += "<a>";
    for (int i = 0; i < 100; ++i) xml += "</a>";

    NullHandler h;
    EXPECT_NO_THROW(fast_parse(xml, h));
}

TEST(FastSax, TooManyAttributes) {
    std::string xml = "<root";
    for (int i = 0; i < 1500; ++i) {
        xml += " attr" + std::to_string(i) + "=\"val\"";
    }
    xml += "/>";

    FastSaxParser<ParseMode::Normal> parser;
    parser.set_max_depth(512);
    parser.set_max_attribute_count(1000);
    NullHandler h;
    EXPECT_THROW(parser.parse(xml.data(), xml.size(), h), SecurityError);
}

TEST(FastSax, EntityExpansionLimit) {
    std::string xml = "<root>";
    for (int i = 0; i < 20000; ++i) xml += "&amp;";
    xml += "</root>";

    FastSaxParser<ParseMode::Normal> parser;
    parser.set_max_entity_expansions(10000);
    NullHandler h;
    EXPECT_THROW(parser.parse(xml.data(), xml.size(), h), SecurityError);
}

// ── Cross-quoted attributes ──────────────────────────────────────────

TEST(FastSax, CrossQuotedAttributes) {
    std::string xml = R"(<a x="it's"/><b y='say "hi"'>text</b>)";
    RecordingHandler h;
    fast_parse(xml, h);

    // Should find both elements
    int starts = 0, ends = 0;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Start) ++starts;
        if (e.type == RecordingHandler::Event::End) ++ends;
    }
    EXPECT_EQ(starts, 2);
    EXPECT_EQ(ends, 2);

    // Check attribute values
    ASSERT_EQ(h.events[0].attrs.size(), 1u);
    EXPECT_EQ(h.events[0].attrs[0].second, "it's");
    ASSERT_EQ(h.events[2].attrs.size(), 1u);
    EXPECT_EQ(h.events[2].attrs[0].second, R"(say "hi")");
}

// ── DOCTYPE handling ─────────────────────────────────────────────────

TEST(FastSax, DocTypeWithInternalSubset) {
    std::string xml =
        "<!DOCTYPE note [\n"
        "  <!ELEMENT note (to,from)>\n"
        "]>\n"
        "<note><to>User</to></note>";
    RecordingHandler h;
    fast_parse(xml, h);

    // Should parse elements after DOCTYPE
    int starts = 0;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Start) ++starts;
    }
    EXPECT_EQ(starts, 2);  // note, to
}

// ── Easy API tests ───────────────────────────────────────────────────

TEST(FastSax, EasyApiBasic) {
    std::string xml = "<root><item id=\"1\">text</item></root>";
    FastSaxParserEasy parser;

    std::vector<std::string> starts, ends;
    parser.on_start_element([&](std::string_view name, std::span<const Attribute> attrs) {
        starts.push_back(std::string(name));
    });
    parser.on_end_element([&](std::string_view name) {
        ends.push_back(std::string(name));
    });
    parser.parse_string(xml);

    ASSERT_EQ(starts.size(), 2u);
    EXPECT_EQ(starts[0], "root");
    EXPECT_EQ(starts[1], "item");
    ASSERT_EQ(ends.size(), 2u);

    EXPECT_EQ(parser.stats().elements, 2u);
    EXPECT_EQ(parser.stats().attributes, 1u);
}

// ── Empty / edge cases ───────────────────────────────────────────────

TEST(FastSax, EmptyElement) {
    std::string xml = "<root></root>";
    RecordingHandler h;
    fast_parse(xml, h);

    ASSERT_EQ(h.events.size(), 2u);
    EXPECT_EQ(h.events[0].type, RecordingHandler::Event::Start);
    EXPECT_EQ(h.events[1].type, RecordingHandler::Event::End);
}

TEST(FastSax, WhitespaceText) {
    std::string xml = "<root>  \n\t  </root>";
    RecordingHandler h;
    fast_parse(xml, h);

    bool found_text = false;
    for (auto& e : h.events) {
        if (e.type == RecordingHandler::Event::Text) {
            EXPECT_EQ(e.text, "  \n\t  ");
            found_text = true;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(FastSax, MultipleSiblings) {
    std::string xml = "<root><a/><b/><c/></root>";
    CountingHandler h;
    fast_parse(xml, h);
    EXPECT_EQ(h.elements, 4u);
}

TEST(FastSax, MixedContent) {
    std::string xml = "<p>Hello <b>world</b>!</p>";
    RecordingHandler h;
    fast_parse(xml, h);

    // p start, text "Hello ", b start, text "world", b end, text "!", p end
    ASSERT_EQ(h.events.size(), 7u);
    EXPECT_EQ(h.events[0].name, "p");
    EXPECT_EQ(h.events[1].text, "Hello ");
    EXPECT_EQ(h.events[2].name, "b");
    EXPECT_EQ(h.events[3].text, "world");
    EXPECT_EQ(h.events[4].name, "b");
    EXPECT_EQ(h.events[5].text, "!");
    EXPECT_EQ(h.events[6].name, "p");
}
