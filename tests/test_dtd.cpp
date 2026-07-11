/// @file test_dtd.cpp
/// @brief Unit tests for DTD parsing and validation.

#include <parshred/dtd.hpp>
#include <gtest/gtest.h>
#include <string>

using namespace parshred;

// ── Entity Declarations ──────────────────────────────────────────────

TEST(DTD, ParseEntityDecl) {
    auto dtd = parse_dtd(R"(
        <!ENTITY copyright "Copyright 2024 Acme Corp.">
        <!ENTITY author "John Doe">
    )");
    
    ASSERT_NE(dtd.find_entity("copyright"), nullptr);
    EXPECT_EQ(dtd.find_entity("copyright")->value, "Copyright 2024 Acme Corp.");
    ASSERT_NE(dtd.find_entity("author"), nullptr);
    EXPECT_EQ(dtd.find_entity("author")->value, "John Doe");
    EXPECT_EQ(dtd.find_entity("unknown"), nullptr);
}

TEST(DTD, ParseParameterEntity) {
    auto dtd = parse_dtd(R"(
        <!ENTITY % common "id ID #REQUIRED">
    )");
    
    auto* ent = dtd.find_entity("common");
    ASSERT_NE(ent, nullptr);
    EXPECT_TRUE(ent->is_parameter);
    EXPECT_EQ(ent->value, "id ID #REQUIRED");
}

// ── Element Declarations ─────────────────────────────────────────────

TEST(DTD, ParseElementEmpty) {
    auto dtd = parse_dtd(R"(
        <!ELEMENT br EMPTY>
    )");
    
    auto* elem = dtd.find_element("br");
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->model, ContentModel::Empty);
}

TEST(DTD, ParseElementAny) {
    auto dtd = parse_dtd(R"(
        <!ELEMENT root ANY>
    )");
    
    auto* elem = dtd.find_element("root");
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->model, ContentModel::Any);
}

TEST(DTD, ParseElementChildren) {
    auto dtd = parse_dtd(R"(
        <!ELEMENT note (to, from, heading, body)>
    )");
    
    auto* elem = dtd.find_element("note");
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->model, ContentModel::Children);
    EXPECT_EQ(elem->content_spec, "(to, from, heading, body)");
}

TEST(DTD, ParseElementMixed) {
    auto dtd = parse_dtd(R"(
        <!ELEMENT p (#PCDATA | b | i)*>
    )");
    
    auto* elem = dtd.find_element("p");
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->model, ContentModel::Mixed);
}

// ── Attribute Declarations ───────────────────────────────────────────

TEST(DTD, ParseAttlistCData) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST img src CDATA #REQUIRED>
    )");
    
    auto* attrs = dtd.find_attlist("img");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(attrs->size(), 1u);
    EXPECT_EQ((*attrs)[0].name, "src");
    EXPECT_EQ((*attrs)[0].type, AttrType::CData);
    EXPECT_EQ((*attrs)[0].default_kind, AttrDefault::Required);
}

TEST(DTD, ParseAttlistId) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST element id ID #IMPLIED>
    )");
    
    auto* attrs = dtd.find_attlist("element");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(attrs->size(), 1u);
    EXPECT_EQ((*attrs)[0].type, AttrType::Id);
    EXPECT_EQ((*attrs)[0].default_kind, AttrDefault::Implied);
}

TEST(DTD, ParseAttlistEnumeration) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST input type (text|password|email) "text">
    )");
    
    auto* attrs = dtd.find_attlist("input");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(attrs->size(), 1u);
    EXPECT_EQ((*attrs)[0].name, "type");
    EXPECT_EQ((*attrs)[0].type, AttrType::Enumeration);
    EXPECT_EQ((*attrs)[0].default_kind, AttrDefault::Value);
    EXPECT_EQ((*attrs)[0].default_value, "text");
    ASSERT_EQ((*attrs)[0].enum_values.size(), 3u);
    EXPECT_EQ((*attrs)[0].enum_values[0], "text");
    EXPECT_EQ((*attrs)[0].enum_values[1], "password");
    EXPECT_EQ((*attrs)[0].enum_values[2], "email");
}

TEST(DTD, ParseAttlistFixed) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST doc version CDATA #FIXED "1.0">
    )");
    
    auto* attrs = dtd.find_attlist("doc");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(attrs->size(), 1u);
    EXPECT_EQ((*attrs)[0].default_kind, AttrDefault::Fixed);
    EXPECT_EQ((*attrs)[0].default_value, "1.0");
}

TEST(DTD, ParseMultipleAttrs) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST person
            name CDATA #REQUIRED
            age CDATA #IMPLIED
            gender (male|female) #IMPLIED>
    )");
    
    auto* attrs = dtd.find_attlist("person");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(attrs->size(), 3u);
    EXPECT_EQ((*attrs)[0].name, "name");
    EXPECT_EQ((*attrs)[1].name, "age");
    EXPECT_EQ((*attrs)[2].name, "gender");
}

// ── Validation ───────────────────────────────────────────────────────

TEST(DTD, ValidateRequiredAttrPresent) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST img src CDATA #REQUIRED>
    )");
    
    std::vector<std::pair<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>>> elements = {
        {"img", {{"src", "photo.jpg"}}},
    };
    
    auto errors = validate(dtd, elements);
    EXPECT_TRUE(errors.empty());
}

TEST(DTD, ValidateRequiredAttrMissing) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST img src CDATA #REQUIRED>
    )");
    
    std::vector<std::pair<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>>> elements = {
        {"img", {}},  // Missing required 'src'
    };
    
    auto errors = validate(dtd, elements);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].kind, ValidationError::Kind::RequiredAttributeMissing);
}

TEST(DTD, ValidateEnumValue) {
    auto dtd = parse_dtd(R"(
        <!ATTLIST input type (text|password|email) "text">
    )");
    
    // Valid value
    std::vector<std::pair<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>>> valid = {
        {"input", {{"type", "password"}}},
    };
    EXPECT_TRUE(validate(dtd, valid).empty());
    
    // Invalid value
    std::vector<std::pair<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>>> invalid = {
        {"input", {{"type", "invalid"}}},
    };
    auto errors = validate(dtd, invalid);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].kind, ValidationError::Kind::InvalidAttributeValue);
}

// ── Well-formedness ──────────────────────────────────────────────────

TEST(DTD, WellformedValid) {
    auto issues = check_wellformedness("<root><child/><child>text</child></root>");
    EXPECT_TRUE(issues.empty());
}

TEST(DTD, WellformedMismatchedTags) {
    auto issues = check_wellformedness("<root><a></b></root>");
    ASSERT_GE(issues.size(), 1u);
    EXPECT_TRUE(issues[0].find("Mismatched") != std::string::npos);
}

TEST(DTD, WellformedUnclosedTag) {
    auto issues = check_wellformedness("<root><child>");
    ASSERT_GE(issues.size(), 1u);
    EXPECT_TRUE(issues[0].find("Unclosed") != std::string::npos ||
                issues[1].find("Unclosed") != std::string::npos);
}

TEST(DTD, WellformedSelfClosing) {
    auto issues = check_wellformedness("<root><br/><hr/></root>");
    EXPECT_TRUE(issues.empty());
}

TEST(DTD, WellformedWithComment) {
    auto issues = check_wellformedness("<root><!-- comment --><child/></root>");
    EXPECT_TRUE(issues.empty());
}

TEST(DTD, WellformedWithPI) {
    auto issues = check_wellformedness("<?xml version=\"1.0\"?><root/>");
    EXPECT_TRUE(issues.empty());
}

TEST(DTD, WellformedComplexDocument) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
        <catalog>
            <book id="bk101">
                <author>Gambardella, Matthew</author>
                <title>XML Developer's Guide</title>
                <price>44.95</price>
            </book>
            <book id="bk102">
                <author>Ralls, Kim</author>
                <title>Midnight Rain</title>
                <price>5.95</price>
            </book>
        </catalog>
    )";
    auto issues = check_wellformedness(xml);
    EXPECT_TRUE(issues.empty());
}
