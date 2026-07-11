/// @file test_pull_parser.cpp
/// @brief Unit tests for the StAX-style XmlReader pull parser.

#include <parshred/pull_parser.hpp>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace parshred;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Collect the sequence of events (as human-readable strings) from a document.
static std::vector<std::string> collect_events(std::string_view xml) {
    std::vector<std::string> out;
    XmlReader reader(xml);
    while (reader.next()) {
        switch (reader.event_type()) {
            case XmlEvent::StartDocument:
                out.push_back("StartDocument");
                break;
            case XmlEvent::EndDocument:
                out.push_back("EndDocument");
                break;
            case XmlEvent::StartElement:
                out.push_back("StartElement(" + std::string(reader.name()) + ")");
                break;
            case XmlEvent::EndElement:
                out.push_back("EndElement(" + std::string(reader.name()) + ")");
                break;
            case XmlEvent::Text:
                out.push_back("Text(" + std::string(reader.text()) + ")");
                break;
            case XmlEvent::CData:
                out.push_back("CData(" + std::string(reader.text()) + ")");
                break;
            case XmlEvent::Comment:
                out.push_back("Comment(" + std::string(reader.text()) + ")");
                break;
            case XmlEvent::ProcessingInstruction:
                out.push_back("PI(" + std::string(reader.name()) + "," +
                              std::string(reader.value()) + ")");
                break;
            case XmlEvent::XmlDeclaration:
                out.push_back("XmlDecl(version=" + std::string(reader.name()) +
                              ",encoding=" + std::string(reader.text()) + ")");
                break;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Basic iteration through a simple document
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, BasicIteration) {
    const std::string xml = "<root><child>hello</child></root>";
    auto events = collect_events(xml);

    // Expected:
    //  StartDocument
    //  StartElement(root)
    //  StartElement(child)
    //  Text(hello)
    //  EndElement(child)
    //  EndElement(root)
    ASSERT_EQ(events.size(), 6u);
    EXPECT_EQ(events[0], "StartDocument");
    EXPECT_EQ(events[1], "StartElement(root)");
    EXPECT_EQ(events[2], "StartElement(child)");
    EXPECT_EQ(events[3], "Text(hello)");
    EXPECT_EQ(events[4], "EndElement(child)");
    EXPECT_EQ(events[5], "EndElement(root)");
}

TEST(XmlReader, ConstructorFromPtrLen) {
    const std::string xml = "<a/>";
    XmlReader reader(xml.data(), xml.size());
    EXPECT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartDocument);
    EXPECT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "a");
}

TEST(XmlReader, ConstructorFromStringView) {
    std::string_view xml = "<z/>";
    XmlReader reader(xml);
    reader.next();  // StartDocument
    reader.next();  // StartElement
    EXPECT_EQ(reader.name(), "z");
}

TEST(XmlReader, FirstEventIsStartDocument) {
    XmlReader reader("<r/>");
    EXPECT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartDocument);
}

TEST(XmlReader, NextReturnsFalseAtEnd) {
    XmlReader reader("<r/>");
    while (reader.next()) { /* drain */ }
    EXPECT_FALSE(reader.next());
    EXPECT_FALSE(reader.next());  // idempotent
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Attribute access
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, AttributeCount) {
    XmlReader reader(R"(<tag a="1" b="2" c="3"/>)");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.attribute_count(), 3u);
}

TEST(XmlReader, AttributeNameAndValue) {
    XmlReader reader(R"(<item id="42" name="foo"/>)");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    ASSERT_EQ(reader.attribute_count(), 2u);
    EXPECT_EQ(reader.attribute_name(0), "id");
    EXPECT_EQ(reader.attribute_value(0), "42");
    EXPECT_EQ(reader.attribute_name(1), "name");
    EXPECT_EQ(reader.attribute_value(1), "foo");
}

TEST(XmlReader, AttributeByName) {
    XmlReader reader(R"(<elem x="10" y="20"/>)");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    EXPECT_EQ(reader.attribute("x"), "10");
    EXPECT_EQ(reader.attribute("y"), "20");
    EXPECT_EQ(reader.attribute("z"), "");  // missing → empty
}

TEST(XmlReader, SingleQuotedAttributes) {
    XmlReader reader(R"(<tag a='hello' b='world'/>)");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    EXPECT_EQ(reader.attribute("a"), "hello");
    EXPECT_EQ(reader.attribute("b"), "world");
}

TEST(XmlReader, ZeroAttributes) {
    XmlReader reader("<bare/>");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    EXPECT_EQ(reader.attribute_count(), 0u);
    EXPECT_EQ(reader.attribute("anything"), "");
}

TEST(XmlReader, NineAttributesSpillsOffInlineStorage) {
    // kInlineAttrs == 8; a 9-attribute element triggers the spill path.
    std::string xml = "<e";
    for (int i = 0; i < 9; ++i)
        xml += " a" + std::to_string(i) + "=\"v" + std::to_string(i) + "\"";
    xml += "/>";

    XmlReader reader(xml);
    reader.next();  // StartDocument
    reader.next();  // StartElement
    ASSERT_EQ(reader.attribute_count(), 9u);
    for (size_t i = 0; i < 9; ++i) {
        EXPECT_EQ(reader.attribute_name(i), "a" + std::to_string(i));
        EXPECT_EQ(reader.attribute_value(i), "v" + std::to_string(i));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. read_element_text()
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, ReadElementTextSimple) {
    XmlReader reader("<root>hello world</root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "hello world");
    // After read_element_text we should be past </root>
    EXPECT_FALSE(reader.next());
}

TEST(XmlReader, ReadElementTextConcatenatesCData) {
    XmlReader reader("<root>foo<![CDATA[bar]]>baz</root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "foobarbaz");
}

TEST(XmlReader, ReadElementTextIgnoresChildElements) {
    // read_element_text on the *outer* element skips inner markup
    // and only collects direct text children at depth 1.
    XmlReader reader("<outer>before<inner>inside</inner>after</outer>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(outer)
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "beforeafter");
}

TEST(XmlReader, ReadElementTextEmptyElement) {
    XmlReader reader("<empty></empty>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(empty)
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "");
}

TEST(XmlReader, ReadElementTextAdvancesPastEndTag) {
    XmlReader reader("<root><item>text</item><next/></root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root) — depth=1
    reader.next();  // StartElement(item) — depth=2
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "text");
    // Now we should be past </item>; the next event is StartElement(next)
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "next");
}

TEST(XmlReader, ReadElementTextOnSelfClosing) {
    XmlReader reader("<br/>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(br) — self-closing
    std::string text = reader.read_element_text();
    EXPECT_EQ(text, "");
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. skip_element()
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, SkipElementSimple) {
    XmlReader reader("<root><skip>content</skip><keep/></root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    reader.next();  // StartElement(skip)
    reader.skip_element();
    // skip_element() consumed </skip>; next should be StartElement(keep)
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "keep");
}

TEST(XmlReader, SkipElementNested) {
    const std::string xml =
        "<root>"
          "<a><b><c>deep</c></b></a>"
          "<after/>"
        "</root>";
    XmlReader reader(xml);
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    reader.next();  // StartElement(a)
    reader.skip_element();
    // All of <b>, <c>, etc. should be consumed
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "after");
}

TEST(XmlReader, SkipElementSelfClosing) {
    XmlReader reader("<root><self/><next/></root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    reader.next();  // StartElement(self)  — self-closing
    reader.skip_element();
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "next");
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Depth tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, DepthAtDocumentLevel) {
    XmlReader reader("<root/>");
    reader.next();  // StartDocument
    EXPECT_EQ(reader.depth(), 0u);
}

TEST(XmlReader, DepthInsideElement) {
    // Self-closing tags (<c/>) do not increase net depth: they fire
    // StartElement (no depth increment) then a synthetic EndElement
    // (no depth decrement).  Only normal open/close pairs change depth.
    XmlReader reader("<a><b><c/></b></a>");
    reader.next();  // StartDocument  depth=0
    EXPECT_EQ(reader.depth(), 0u);

    reader.next();  // StartElement(a)  depth → 1
    EXPECT_EQ(reader.depth(), 1u);

    reader.next();  // StartElement(b)  depth → 2
    EXPECT_EQ(reader.depth(), 2u);

    // Self-closing <c/> – StartElement fires; depth stays at 2 (not incremented
    // for self-closing tags so that net depth effect is zero).
    reader.next();  // StartElement(c) — self-closing
    EXPECT_EQ(reader.depth(), 2u);

    reader.next();  // EndElement(c) — synthetic; depth stays at 2
    EXPECT_EQ(reader.depth(), 2u);

    reader.next();  // EndElement(b)  depth → 1
    EXPECT_EQ(reader.depth(), 1u);

    reader.next();  // EndElement(a)  depth → 0
    EXPECT_EQ(reader.depth(), 0u);
}

TEST(XmlReader, DepthWithNormalStartEnd) {
    XmlReader reader("<outer><inner>text</inner></outer>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(outer) → depth=1
    EXPECT_EQ(reader.depth(), 1u);
    reader.next();  // StartElement(inner) → depth=2
    EXPECT_EQ(reader.depth(), 2u);
    reader.next();  // Text
    reader.next();  // EndElement(inner) → depth=1
    EXPECT_EQ(reader.depth(), 1u);
    reader.next();  // EndElement(outer) → depth=0
    EXPECT_EQ(reader.depth(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Self-closing elements
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, SelfClosingBothEvents) {
    XmlReader reader("<br/>");
    reader.next();  // StartDocument
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "br");

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::EndElement);
    EXPECT_EQ(reader.name(), "br");

    EXPECT_FALSE(reader.next());
}

TEST(XmlReader, SelfClosingWithAttributes) {
    XmlReader reader(R"(<img src="photo.jpg" alt="A photo"/>)");
    reader.next();  // StartDocument
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "img");
    EXPECT_EQ(reader.attribute("src"), "photo.jpg");
    EXPECT_EQ(reader.attribute("alt"), "A photo");

    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::EndElement);
    EXPECT_EQ(reader.name(), "img");
}

TEST(XmlReader, MultipleSelfClosing) {
    XmlReader reader("<root><a/><b/><c/></root>");
    std::vector<std::string> ev;
    while (reader.next()) ev.push_back(std::string(reader.name()));
    // StartDocument has empty name; filter it
    // Expected names in order: "", root, a, a, b, b, c, c, root
    EXPECT_EQ(ev.size(), 9u);
    EXPECT_EQ(ev[1], "root");
    EXPECT_EQ(ev[2], "a");
    EXPECT_EQ(ev[3], "a");
    EXPECT_EQ(ev[4], "b");
    EXPECT_EQ(ev[5], "b");
    EXPECT_EQ(ev[6], "c");
    EXPECT_EQ(ev[7], "c");
    EXPECT_EQ(ev[8], "root");
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. CDATA sections
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, CData) {
    XmlReader reader("<root><![CDATA[raw <data> & stuff]]></root>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::CData);
    EXPECT_EQ(reader.text(), "raw <data> & stuff");
}

TEST(XmlReader, CDataEventType) {
    std::string xml = "<r><![CDATA[x]]></r>";
    bool found = false;
    XmlReader reader(xml);
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::CData) {
            EXPECT_EQ(reader.text(), "x");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(XmlReader, CDataPreservesMarkupChars) {
    XmlReader reader("<r><![CDATA[</not-a-tag>]]></r>");
    reader.next();
    reader.next();  // StartElement
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::CData);
    EXPECT_EQ(reader.text(), "</not-a-tag>");
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Comments
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, Comment) {
    XmlReader reader("<root><!-- a comment --></root>");
    bool found = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::Comment) {
            EXPECT_EQ(reader.text(), " a comment ");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(XmlReader, CommentBeforeRoot) {
    XmlReader reader("<!-- header --><root/>");
    bool found_comment = false;
    bool found_root    = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::Comment) {
            EXPECT_EQ(reader.text(), " header ");
            found_comment = true;
        }
        if (reader.is_start_element("root")) found_root = true;
    }
    EXPECT_TRUE(found_comment);
    EXPECT_TRUE(found_root);
}

TEST(XmlReader, MultipleComments) {
    XmlReader reader("<r><!--one--><!--two--></r>");
    std::vector<std::string> comments;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::Comment)
            comments.push_back(std::string(reader.text()));
    }
    ASSERT_EQ(comments.size(), 2u);
    EXPECT_EQ(comments[0], "one");
    EXPECT_EQ(comments[1], "two");
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Processing instructions
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, ProcessingInstruction) {
    XmlReader reader("<?mypi some data?><root/>");
    bool found = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::ProcessingInstruction) {
            EXPECT_EQ(reader.name(), "mypi");
            EXPECT_EQ(reader.value(), "some data");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(XmlReader, ProcessingInstructionNoData) {
    XmlReader reader("<?standalone?><r/>");
    bool found = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::ProcessingInstruction) {
            EXPECT_EQ(reader.name(), "standalone");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(XmlReader, XmlDeclaration) {
    XmlReader reader(R"(<?xml version="1.0" encoding="UTF-8"?><root/>)");
    bool found = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::XmlDeclaration) {
            EXPECT_EQ(reader.name(), "1.0");   // version via name()
            EXPECT_EQ(reader.text(), "UTF-8"); // encoding via text()
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(XmlReader, XmlDeclarationMinimal) {
    XmlReader reader(R"(<?xml version="1.1"?><doc/>)");
    bool found = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::XmlDeclaration) {
            EXPECT_EQ(reader.name(), "1.1");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Empty document / edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, EmptyInput) {
    XmlReader reader("");
    // next() should return true once (StartDocument) then false
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartDocument);
    EXPECT_FALSE(reader.next());
}

TEST(XmlReader, WhitespaceOnlyInput) {
    XmlReader reader("   \n\t  ");
    reader.next();  // StartDocument
    // Only whitespace text, no elements — next() eventually returns false.
    bool got_text = false;
    while (reader.next()) {
        if (reader.event_type() == XmlEvent::Text) got_text = true;
    }
    EXPECT_TRUE(got_text);  // whitespace is still reported as Text
}

TEST(XmlReader, EmptyElement) {
    auto events = collect_events("<root></root>");
    EXPECT_EQ(events[0], "StartDocument");
    EXPECT_EQ(events[1], "StartElement(root)");
    EXPECT_EQ(events[2], "EndElement(root)");
}

TEST(XmlReader, SingleSelfClosingOnlyDocument) {
    auto events = collect_events("<item/>");
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0], "StartDocument");
    EXPECT_EQ(events[1], "StartElement(item)");
    EXPECT_EQ(events[2], "EndElement(item)");
}

TEST(XmlReader, DoctypeIsSkipped) {
    const std::string xml =
        "<!DOCTYPE note [\n"
        "  <!ELEMENT note (to,from)>\n"
        "]>\n"
        "<note><to>User</to></note>";
    std::vector<std::string> names;
    XmlReader reader(xml);
    while (reader.next()) {
        if (reader.is_start_element())
            names.push_back(std::string(reader.name()));
    }
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "note");
    EXPECT_EQ(names[1], "to");
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. Nested elements with the same name
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, NestedSameName) {
    const std::string xml = "<item><item><item>deep</item></item></item>";
    std::vector<XmlEvent> events;
    XmlReader reader(xml);
    while (reader.next()) events.push_back(reader.event_type());

    // StartDocument + 3×Start + Text + 3×End
    ASSERT_EQ(events.size(), 8u);
    EXPECT_EQ(events[0], XmlEvent::StartDocument);
    EXPECT_EQ(events[1], XmlEvent::StartElement);
    EXPECT_EQ(events[2], XmlEvent::StartElement);
    EXPECT_EQ(events[3], XmlEvent::StartElement);
    EXPECT_EQ(events[4], XmlEvent::Text);
    EXPECT_EQ(events[5], XmlEvent::EndElement);
    EXPECT_EQ(events[6], XmlEvent::EndElement);
    EXPECT_EQ(events[7], XmlEvent::EndElement);
}

TEST(XmlReader, NestedSameNameReadElementText) {
    // read_element_text on the outermost <item> must count depth properly
    // so it doesn't stop at the first </item>.
    XmlReader reader("<item><item>inner</item></item>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(outer item)
    std::string text = reader.read_element_text();
    // No direct text children at depth==1 — only a child element.
    EXPECT_EQ(text, "");
    EXPECT_FALSE(reader.next());  // fully consumed
}

TEST(XmlReader, NestedSameNameSkipElement) {
    XmlReader reader("<wrap><item><item>x</item></item><sibling/></wrap>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(wrap)
    reader.next();  // StartElement(outer item)
    reader.skip_element();
    // Should land on StartElement(sibling)
    ASSERT_TRUE(reader.next());
    EXPECT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "sibling");
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. Line / column tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, LineColumnStart) {
    XmlReader reader("<root/>");
    reader.next();  // StartDocument
    EXPECT_EQ(reader.line(), 1u);
    EXPECT_EQ(reader.column(), 1u);
}

TEST(XmlReader, LineColumnAfterNewlines) {
    //  Line 1: "<!-- comment -->\n"
    //  Line 2: "<root/>"
    const std::string xml = "<!-- comment -->\n<root/>";
    XmlReader reader(xml);
    reader.next();  // StartDocument
    reader.next();  // Comment (line 1)
    EXPECT_EQ(reader.line(), 1u);

    reader.next();  // Text "\n" or StartElement(root)
    // Skip any text events (the "\n" between comment and root may appear)
    while (reader.event_type() == XmlEvent::Text) reader.next();

    // Now we should be at StartElement(root) on line 2
    ASSERT_EQ(reader.event_type(), XmlEvent::StartElement);
    EXPECT_EQ(reader.name(), "root");
    EXPECT_EQ(reader.line(), 2u);
}

TEST(XmlReader, ColumnTracking) {
    //  "<root>" starts at column 1
    const std::string xml = "<root/>";
    XmlReader reader(xml);
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    EXPECT_EQ(reader.line(), 1u);
    EXPECT_EQ(reader.column(), 1u);
}

TEST(XmlReader, LineTrackingMultipleLines) {
    const std::string xml =
        "<root>\n"       // line 1
        "  <a/>\n"       // line 2
        "  <b/>\n"       // line 3
        "</root>";       // line 4
    XmlReader reader(xml);
    reader.next();  // StartDocument

    // Consume events and collect (line, event) pairs for start elements
    std::vector<std::pair<size_t, std::string>> items;
    while (reader.next()) {
        if (reader.is_start_element())
            items.push_back({reader.line(), std::string(reader.name())});
    }
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].second, "root"); EXPECT_EQ(items[0].first, 1u);
    EXPECT_EQ(items[1].second, "a");    EXPECT_EQ(items[1].first, 2u);
    EXPECT_EQ(items[2].second, "b");    EXPECT_EQ(items[2].first, 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. Convenience predicates
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, IsStartElementOverloads) {
    XmlReader reader("<foo/>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(foo)
    EXPECT_TRUE(reader.is_start_element());
    EXPECT_TRUE(reader.is_start_element("foo"));
    EXPECT_FALSE(reader.is_start_element("bar"));
    EXPECT_FALSE(reader.is_end_element());
}

TEST(XmlReader, IsEndElementOverloads) {
    XmlReader reader("<foo/>");
    reader.next();  // StartDocument
    reader.next();  // StartElement(foo)
    reader.next();  // EndElement(foo) — synthetic self-close
    EXPECT_TRUE(reader.is_end_element());
    EXPECT_TRUE(reader.is_end_element("foo"));
    EXPECT_FALSE(reader.is_end_element("bar"));
    EXPECT_FALSE(reader.is_start_element());
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. Mixed content / realistic usage pattern
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, RealisticUsagePattern) {
    const std::string xml =
        R"(<?xml version="1.0"?>)"
        "<catalog>"
          R"(<item id="1"><title>Alpha</title></item>)"
          R"(<item id="2"><title>Beta</title></item>)"
        "</catalog>";

    std::vector<std::pair<std::string, std::string>> items;  // id → title

    XmlReader reader(xml);
    while (reader.next()) {
        if (reader.is_start_element("item")) {
            std::string id(reader.attribute("id"));
            reader.next();               // StartElement(title)
            std::string title = reader.read_element_text();
            items.push_back({id, title});
        }
    }

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].first,  "1");
    EXPECT_EQ(items[0].second, "Alpha");
    EXPECT_EQ(items[1].first,  "2");
    EXPECT_EQ(items[1].second, "Beta");
}

TEST(XmlReader, MixedTextAndElements) {
    XmlReader reader("<p>Hello <b>world</b>!</p>");
    auto events = collect_events("<p>Hello <b>world</b>!</p>");
    // StartDocument, StartElement(p), Text(Hello ), StartElement(b),
    // Text(world), EndElement(b), Text(!), EndElement(p)
    ASSERT_EQ(events.size(), 8u);
    EXPECT_EQ(events[2], "Text(Hello )");
    EXPECT_EQ(events[3], "StartElement(b)");
    EXPECT_EQ(events[4], "Text(world)");
    EXPECT_EQ(events[5], "EndElement(b)");
    EXPECT_EQ(events[6], "Text(!)");
}

// ─────────────────────────────────────────────────────────────────────────────
// 15. Error handling — malformed input
// ─────────────────────────────────────────────────────────────────────────────

TEST(XmlReader, MissingClosingAngleBracketStartTag) {
    XmlReader reader("<root");
    reader.next();  // StartDocument
    EXPECT_THROW(reader.next(), ParseError);
}

TEST(XmlReader, MissingClosingAngleBracketEndTag) {
    XmlReader reader("<root></root");
    reader.next();  // StartDocument
    reader.next();  // StartElement(root)
    EXPECT_THROW(reader.next(), ParseError);
}
