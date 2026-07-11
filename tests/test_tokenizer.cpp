/// @file test_tokenizer.cpp
/// @brief Unit tests for the XML tokenizer.

#include <parshred/tokenizer.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace parshred;

TEST(Tokenizer, EmptyInput) {
    Tokenizer tok;
    std::string input;
    tok.tokenize({input.data(), input.size()});
    EXPECT_EQ(tok.size(), 0u);
}

TEST(Tokenizer, SingleSelfClosingTag) {
    std::string input = "<br/>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 1u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::SelfClosingTag);
    EXPECT_EQ(tok.tokens()[0].text, "br");
}

TEST(Tokenizer, StartAndEndTag) {
    std::string input = "<root></root>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 2u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::StartTag);
    EXPECT_EQ(tok.tokens()[0].text, "root");
    EXPECT_EQ(tok.tokens()[1].type, TokenType::EndTag);
    EXPECT_EQ(tok.tokens()[1].text, "root");
}

TEST(Tokenizer, TagWithText) {
    std::string input = "<p>Hello World</p>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 3u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::StartTag);
    EXPECT_EQ(tok.tokens()[0].text, "p");

    // Find the text token
    bool found_text = false;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::Text && t.text == "Hello World") {
            found_text = true;
            break;
        }
    }
    EXPECT_TRUE(found_text);
}

TEST(Tokenizer, TagWithAttributes) {
    std::string input = R"(<div class="main" id="top">content</div>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 4u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::StartTag);
    EXPECT_EQ(tok.tokens()[0].text, "div");

    // Check attributes
    EXPECT_EQ(tok.tokens()[1].type, TokenType::AttributeName);
    EXPECT_EQ(tok.tokens()[1].text, "class");
    EXPECT_EQ(tok.tokens()[2].type, TokenType::AttributeValue);
    EXPECT_EQ(tok.tokens()[2].text, "main");
    EXPECT_EQ(tok.tokens()[3].type, TokenType::AttributeName);
    EXPECT_EQ(tok.tokens()[3].text, "id");
    EXPECT_EQ(tok.tokens()[4].type, TokenType::AttributeValue);
    EXPECT_EQ(tok.tokens()[4].text, "top");
}

TEST(Tokenizer, Comment) {
    std::string input = "<!-- this is a comment --><root/>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    bool found_comment = false;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::Comment) {
            EXPECT_EQ(t.text, " this is a comment ");
            found_comment = true;
            break;
        }
    }
    EXPECT_TRUE(found_comment);
}

TEST(Tokenizer, CData) {
    std::string input = "<data><![CDATA[some <raw> & content]]></data>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    bool found_cdata = false;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::CData) {
            EXPECT_EQ(t.text, "some <raw> & content");
            found_cdata = true;
            break;
        }
    }
    EXPECT_TRUE(found_cdata);
}

TEST(Tokenizer, ProcessingInstruction) {
    std::string input = R"(<?xml version="1.0"?><root/>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 1u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::XmlDeclaration);
}

TEST(Tokenizer, EntityReference) {
    std::string input = "<p>a &amp; b</p>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    bool found_entity = false;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::EntityRef) {
            EXPECT_EQ(t.text, "&amp;");
            found_entity = true;
            break;
        }
    }
    EXPECT_TRUE(found_entity);
}

TEST(Tokenizer, NestedTags) {
    std::string input = "<a><b><c>deep</c></b></a>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    // Count start and end tags
    int start_count = 0, end_count = 0;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::StartTag) ++start_count;
        if (t.type == TokenType::EndTag) ++end_count;
    }
    EXPECT_EQ(start_count, 3);
    EXPECT_EQ(end_count, 3);
}

TEST(Tokenizer, MixedContent) {
    std::string input = "<p>Hello <b>world</b> end</p>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    // Should have: StartTag(p), Text(Hello ), StartTag(b), Text(world),
    //              EndTag(b), Text( end), EndTag(p)
    EXPECT_GE(tok.size(), 7u);
}

TEST(Tokenizer, SelfClosingWithAttributes) {
    std::string input = R"(<img src="photo.jpg" alt="A photo"/>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 1u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::SelfClosingTag);
    EXPECT_EQ(tok.tokens()[0].text, "img");
}

TEST(Tokenizer, DocTypeSimple) {
    std::string input = "<!DOCTYPE html><root/>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 2u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::DocType);
    EXPECT_EQ(tok.tokens()[0].text, "<!DOCTYPE html>");
    EXPECT_EQ(tok.tokens()[1].type, TokenType::SelfClosingTag);
    EXPECT_EQ(tok.tokens()[1].text, "root");
}

TEST(Tokenizer, DocTypeWithInternalSubset) {
    std::string input =
        "<!DOCTYPE note [\n"
        "  <!ELEMENT note (to,from,body)>\n"
        "  <!ELEMENT to (#PCDATA)>\n"
        "]>\n"
        "<note><to>User</to></note>";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    // The DOCTYPE should be a single token covering everything up to the closing >
    ASSERT_GE(tok.size(), 1u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::DocType);

    // The rest should parse as normal elements
    bool found_note_start = false;
    bool found_to_start = false;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::StartTag && t.text == "note") found_note_start = true;
        if (t.type == TokenType::StartTag && t.text == "to") found_to_start = true;
    }
    EXPECT_TRUE(found_note_start);
    EXPECT_TRUE(found_to_start);
}

TEST(Tokenizer, CrossQuotedAttributes) {
    // Single quote inside double-quoted value
    std::string input = R"(<tag attr="it's"/>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 3u);
    EXPECT_EQ(tok.tokens()[0].type, TokenType::SelfClosingTag);
    EXPECT_EQ(tok.tokens()[0].text, "tag");
    EXPECT_EQ(tok.tokens()[1].type, TokenType::AttributeName);
    EXPECT_EQ(tok.tokens()[1].text, "attr");
    EXPECT_EQ(tok.tokens()[2].type, TokenType::AttributeValue);
    EXPECT_EQ(tok.tokens()[2].text, "it's");
}

TEST(Tokenizer, CrossQuotedDoubleInSingle) {
    // Double quote inside single-quoted value
    std::string input = R"(<tag attr='say "hello"'/>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    ASSERT_GE(tok.size(), 3u);
    EXPECT_EQ(tok.tokens()[2].type, TokenType::AttributeValue);
    EXPECT_EQ(tok.tokens()[2].text, R"(say "hello")");
}

TEST(Tokenizer, CrossQuotedFollowedByTag) {
    // Ensure parsing continues correctly after cross-quoted attributes
    std::string input = R"(<a x="it's"/><b>text</b>)";
    Tokenizer tok;
    tok.tokenize({input.data(), input.size()});

    int start_count = 0, end_count = 0;
    for (const auto& t : tok.tokens()) {
        if (t.type == TokenType::StartTag || t.type == TokenType::SelfClosingTag) ++start_count;
        if (t.type == TokenType::EndTag) ++end_count;
    }
    EXPECT_EQ(start_count, 2); // <a/> and <b>
    EXPECT_EQ(end_count, 1);   // </b> (a is self-closing)
}
