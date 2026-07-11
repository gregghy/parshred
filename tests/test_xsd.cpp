/// @file test_xsd.cpp
/// @brief Unit tests for XSD simple type validation.

#include <parshred/xsd.hpp>
#include <gtest/gtest.h>
#include <string>

using namespace parshred;
using namespace parshred::xsd;

// Helper: parse XML into FastDom for schema tests.
static FastDom parse_test_xml(const std::string& xml) {
    return fast_dom_parse<0>(xml.data(), xml.size());
}

// ── Built-in String Types ────────────────────────────────────────────

TEST(XsdValidate, StringAlwaysValid) {
    EXPECT_TRUE(validate_value("hello", XsdType::String));
    EXPECT_TRUE(validate_value("", XsdType::String));
    EXPECT_TRUE(validate_value("with\nwhitespace", XsdType::String));
}

TEST(XsdValidate, NormalizedStringRejectsControl) {
    EXPECT_TRUE(validate_value("hello world", XsdType::NormalizedString));
    EXPECT_FALSE(validate_value("hello\nworld", XsdType::NormalizedString));
    EXPECT_FALSE(validate_value("hello\tworld", XsdType::NormalizedString));
    EXPECT_FALSE(validate_value("hello\rworld", XsdType::NormalizedString));
}

TEST(XsdValidate, TokenRequiresCollapsedWhitespace) {
    EXPECT_TRUE(validate_value("hello world", XsdType::Token));
    EXPECT_FALSE(validate_value(" hello", XsdType::Token));
    EXPECT_FALSE(validate_value("hello ", XsdType::Token));
    EXPECT_FALSE(validate_value("hello  world", XsdType::Token));
    EXPECT_FALSE(validate_value("hello\tworld", XsdType::Token));
}

// ── Boolean ───────────────────────────────────────────────────────────

TEST(XsdValidate, BooleanValidAndInvalid) {
    EXPECT_TRUE(validate_value("true", XsdType::Boolean));
    EXPECT_TRUE(validate_value("false", XsdType::Boolean));
    EXPECT_TRUE(validate_value("1", XsdType::Boolean));
    EXPECT_TRUE(validate_value("0", XsdType::Boolean));
    EXPECT_TRUE(validate_value("  true  ", XsdType::Boolean));
    EXPECT_FALSE(validate_value("yes", XsdType::Boolean));
    EXPECT_FALSE(validate_value("", XsdType::Boolean));
    EXPECT_FALSE(validate_value("TRUE", XsdType::Boolean));
}

// ── Integer Family ─────────────────────────────────────────────────────

TEST(XsdValidate, Integer) {
    EXPECT_TRUE(validate_value("123", XsdType::Integer));
    EXPECT_TRUE(validate_value("-123", XsdType::Integer));
    EXPECT_TRUE(validate_value("+123", XsdType::Integer));
    EXPECT_TRUE(validate_value("  456  ", XsdType::Integer));
    EXPECT_FALSE(validate_value("12.3", XsdType::Integer));
    EXPECT_FALSE(validate_value("abc", XsdType::Integer));
    EXPECT_FALSE(validate_value("-", XsdType::Integer));
}

TEST(XsdValidate, NonNegativeInteger) {
    EXPECT_TRUE(validate_value("0", XsdType::NonNegativeInteger));
    EXPECT_TRUE(validate_value("42", XsdType::NonNegativeInteger));
    EXPECT_FALSE(validate_value("-1", XsdType::NonNegativeInteger));
    EXPECT_FALSE(validate_value("-0", XsdType::NonNegativeInteger));
}

TEST(XsdValidate, PositiveInteger) {
    EXPECT_TRUE(validate_value("1", XsdType::PositiveInteger));
    EXPECT_FALSE(validate_value("0", XsdType::PositiveInteger));
    EXPECT_FALSE(validate_value("-5", XsdType::PositiveInteger));
    EXPECT_FALSE(validate_value("01", XsdType::PositiveInteger));
}

TEST(XsdValidate, NonPositiveInteger) {
    EXPECT_TRUE(validate_value("0", XsdType::NonPositiveInteger));
    EXPECT_TRUE(validate_value("-10", XsdType::NonPositiveInteger));
    EXPECT_FALSE(validate_value("1", XsdType::NonPositiveInteger));
}

TEST(XsdValidate, NegativeInteger) {
    EXPECT_TRUE(validate_value("-1", XsdType::NegativeInteger));
    EXPECT_FALSE(validate_value("0", XsdType::NegativeInteger));
    EXPECT_FALSE(validate_value("-", XsdType::NegativeInteger));
}

TEST(XsdValidate, IntRange) {
    EXPECT_TRUE(validate_value("2147483647", XsdType::Int));
    EXPECT_TRUE(validate_value("-2147483648", XsdType::Int));
    EXPECT_FALSE(validate_value("2147483648", XsdType::Int));
    EXPECT_FALSE(validate_value("-2147483649", XsdType::Int));
}

TEST(XsdValidate, ShortRange) {
    EXPECT_TRUE(validate_value("32767", XsdType::Short));
    EXPECT_TRUE(validate_value("-32768", XsdType::Short));
    EXPECT_FALSE(validate_value("32768", XsdType::Short));
    EXPECT_FALSE(validate_value("-32769", XsdType::Short));
}

TEST(XsdValidate, ByteRange) {
    EXPECT_TRUE(validate_value("127", XsdType::Byte));
    EXPECT_TRUE(validate_value("-128", XsdType::Byte));
    EXPECT_FALSE(validate_value("128", XsdType::Byte));
    EXPECT_FALSE(validate_value("-129", XsdType::Byte));
}

TEST(XsdValidate, LongRange) {
    EXPECT_TRUE(validate_value("9223372036854775807", XsdType::Long));
    EXPECT_TRUE(validate_value("-9223372036854775808", XsdType::Long));
    EXPECT_FALSE(validate_value("9223372036854775808", XsdType::Long));
}

TEST(XsdValidate, UnsignedIntRange) {
    EXPECT_TRUE(validate_value("4294967295", XsdType::UnsignedInt));
    EXPECT_FALSE(validate_value("4294967296", XsdType::UnsignedInt));
    EXPECT_FALSE(validate_value("-1", XsdType::UnsignedInt));
}

TEST(XsdValidate, UnsignedShortRange) {
    EXPECT_TRUE(validate_value("65535", XsdType::UnsignedShort));
    EXPECT_FALSE(validate_value("65536", XsdType::UnsignedShort));
}

TEST(XsdValidate, UnsignedByteRange) {
    EXPECT_TRUE(validate_value("255", XsdType::UnsignedByte));
    EXPECT_FALSE(validate_value("256", XsdType::UnsignedByte));
}

TEST(XsdValidate, UnsignedLongRange) {
    EXPECT_TRUE(validate_value("18446744073709551615", XsdType::UnsignedLong));
    EXPECT_FALSE(validate_value("18446744073709551616", XsdType::UnsignedLong));
}

// ── Floating Point / Decimal ─────────────────────────────────────────

TEST(XsdValidate, Decimal) {
    EXPECT_TRUE(validate_value("123.45", XsdType::Decimal));
    EXPECT_TRUE(validate_value("-0.5", XsdType::Decimal));
    EXPECT_TRUE(validate_value("42", XsdType::Decimal));
    EXPECT_TRUE(validate_value(".75", XsdType::Decimal));
    EXPECT_TRUE(validate_value("13.", XsdType::Decimal));
    EXPECT_FALSE(validate_value("1.2.3", XsdType::Decimal));
    EXPECT_FALSE(validate_value("abc", XsdType::Decimal));
    EXPECT_FALSE(validate_value(".", XsdType::Decimal));
}

TEST(XsdValidate, Float) {
    EXPECT_TRUE(validate_value("1.5", XsdType::Float));
    EXPECT_TRUE(validate_value("-3.14e10", XsdType::Float));
    EXPECT_TRUE(validate_value("INF", XsdType::Float));
    EXPECT_TRUE(validate_value("-INF", XsdType::Float));
    EXPECT_TRUE(validate_value("NaN", XsdType::Float));
    EXPECT_FALSE(validate_value("1.2.3", XsdType::Float));
    EXPECT_FALSE(validate_value("", XsdType::Float));
}

TEST(XsdValidate, Double) {
    EXPECT_TRUE(validate_value("1.7976931348623157e+308", XsdType::Double));
    EXPECT_FALSE(validate_value("not a number", XsdType::Double));
}

// ── Date / Time ───────────────────────────────────────────────────────

TEST(XsdValidate, Date) {
    EXPECT_TRUE(validate_value("2024-07-11", XsdType::Date));
    EXPECT_TRUE(validate_value("2024-07-11Z", XsdType::Date));
    EXPECT_TRUE(validate_value("2024-07-11+02:00", XsdType::Date));
    EXPECT_FALSE(validate_value("2024-7-11", XsdType::Date));
    EXPECT_FALSE(validate_value("2024/07/11", XsdType::Date));
    EXPECT_FALSE(validate_value("2024-07-11 12:00", XsdType::Date));
}

TEST(XsdValidate, DateTime) {
    EXPECT_TRUE(validate_value("2024-07-11T14:30:00", XsdType::DateTime));
    EXPECT_TRUE(validate_value("2024-07-11T14:30:00.123Z", XsdType::DateTime));
    EXPECT_TRUE(validate_value("2024-07-11T14:30:00+02:00", XsdType::DateTime));
    EXPECT_FALSE(validate_value("2024-07-11 14:30:00", XsdType::DateTime));
    EXPECT_FALSE(validate_value("2024-07-11T14:30", XsdType::DateTime));
}

TEST(XsdValidate, Time) {
    EXPECT_TRUE(validate_value("14:30:00", XsdType::Time));
    EXPECT_TRUE(validate_value("14:30:00.123Z", XsdType::Time));
    EXPECT_TRUE(validate_value("14:30:00+02:00", XsdType::Time));
    EXPECT_FALSE(validate_value("14:30", XsdType::Time));
    EXPECT_FALSE(validate_value("14-30-00", XsdType::Time));
}

TEST(XsdValidate, Duration) {
    EXPECT_TRUE(validate_value("P1Y2M3DT10H30M", XsdType::Duration));
    EXPECT_TRUE(validate_value("P1D", XsdType::Duration));
    EXPECT_TRUE(validate_value("PT1H30M", XsdType::Duration));
    EXPECT_FALSE(validate_value("1Y", XsdType::Duration));
    EXPECT_FALSE(validate_value("P", XsdType::Duration));
    EXPECT_FALSE(validate_value("PT", XsdType::Duration));
}

// ── Names / URI ───────────────────────────────────────────────────────

TEST(XsdValidate, AnyURI) {
    EXPECT_TRUE(validate_value("http://example.com", XsdType::AnyURI));
    EXPECT_TRUE(validate_value("urn:isbn:123", XsdType::AnyURI));
    EXPECT_FALSE(validate_value("", XsdType::AnyURI));
}

TEST(XsdValidate, NCName) {
    EXPECT_TRUE(validate_value("_foo", XsdType::NCName));
    EXPECT_TRUE(validate_value("bar", XsdType::NCName));
    EXPECT_FALSE(validate_value("foo:bar", XsdType::NCName));
    EXPECT_FALSE(validate_value("123", XsdType::NCName));
    EXPECT_FALSE(validate_value("", XsdType::NCName));
}

TEST(XsdValidate, Name) {
    EXPECT_TRUE(validate_value("foo:bar", XsdType::Name));
    EXPECT_TRUE(validate_value("_baz", XsdType::Name));
    EXPECT_FALSE(validate_value("123", XsdType::Name));
}

TEST(XsdValidate, NMTOKEN) {
    EXPECT_TRUE(validate_value("foo:bar-baz.123", XsdType::NMTOKEN));
    EXPECT_TRUE(validate_value("123", XsdType::NMTOKEN));
    EXPECT_FALSE(validate_value("", XsdType::NMTOKEN));
    EXPECT_FALSE(validate_value("foo bar", XsdType::NMTOKEN));
}

TEST(XsdValidate, QName) {
    EXPECT_TRUE(validate_value("ns:local", XsdType::QName));
    EXPECT_TRUE(validate_value("local", XsdType::QName));
    EXPECT_FALSE(validate_value(":local", XsdType::QName));
    EXPECT_FALSE(validate_value("ns:", XsdType::QName));
    EXPECT_FALSE(validate_value("a:b:c", XsdType::QName));
}

TEST(XsdValidate, IDandIDREF) {
    EXPECT_TRUE(validate_value("id1", XsdType::ID));
    EXPECT_TRUE(validate_value("id-1", XsdType::IDREF));
    EXPECT_FALSE(validate_value("1id", XsdType::ID));
    EXPECT_FALSE(validate_value("id:1", XsdType::IDREF));
}

// ── Binary ────────────────────────────────────────────────────────────

TEST(XsdValidate, Base64Binary) {
    EXPECT_TRUE(validate_value("SGVsbG8gV29ybGQh", XsdType::Base64Binary));
    EXPECT_TRUE(validate_value("", XsdType::Base64Binary));
    EXPECT_TRUE(validate_value("QQ==", XsdType::Base64Binary));
    EXPECT_FALSE(validate_value("SGVsbG8gV29ybGQh!", XsdType::Base64Binary));
    EXPECT_FALSE(validate_value("SGVsbG8gV29ybGQ", XsdType::Base64Binary)); // length not multiple of 4
}

TEST(XsdValidate, HexBinary) {
    EXPECT_TRUE(validate_value("deadbeef", XsdType::HexBinary));
    EXPECT_TRUE(validate_value("DEADBEEF", XsdType::HexBinary));
    EXPECT_TRUE(validate_value("dead", XsdType::HexBinary));   // 4 hex chars = 2 bytes, valid
    EXPECT_FALSE(validate_value("dea", XsdType::HexBinary));   // odd length
    EXPECT_FALSE(validate_value("deadbefg", XsdType::HexBinary));  // 'g' not hex
}

// ── Facets ────────────────────────────────────────────────────────────

TEST(XsdFacet, MinLength) {
    XsdSimpleType t{.base_type = XsdType::String, .facets = {}, .name = "min3"};
    t.facets.min_length = 3;
    EXPECT_TRUE(validate_value("abc", t));
    EXPECT_TRUE(validate_value("abcd", t));
    EXPECT_FALSE(validate_value("ab", t));
}

TEST(XsdFacet, MaxLength) {
    XsdSimpleType t{.base_type = XsdType::String, .facets = {}, .name = "max3"};
    t.facets.max_length = 3;
    EXPECT_TRUE(validate_value("abc", t));
    EXPECT_TRUE(validate_value("ab", t));
    EXPECT_FALSE(validate_value("abcd", t));
}

TEST(XsdFacet, Enumeration) {
    XsdSimpleType t{.base_type = XsdType::String, .facets = {}, .name = "color"};
    t.facets.enumeration = {"red", "green", "blue"};
    EXPECT_TRUE(validate_value("green", t));
    EXPECT_FALSE(validate_value("yellow", t));
}

TEST(XsdFacet, Pattern) {
    XsdSimpleType t{.base_type = XsdType::String, .facets = {}, .name = "zip"};
    t.facets.pattern = "[0-9]{5}(-[0-9]{4})?";
    EXPECT_TRUE(validate_value("12345", t));
    EXPECT_TRUE(validate_value("12345-6789", t));
    EXPECT_FALSE(validate_value("1234", t));
    EXPECT_FALSE(validate_value("12345-67", t));
}

TEST(XsdFacet, WhitespaceCollapse) {
    XsdSimpleType t{.base_type = XsdType::String, .facets = {}, .name = "collapsed"};
    t.facets.whitespace_handling = WhitespaceHandling::Collapse;
    t.facets.enumeration = {"hello world"};
    EXPECT_TRUE(validate_value("  hello   world  ", t));  // collapses to "hello world"
    EXPECT_TRUE(validate_value("hello  world", t));       // collapses to "hello world"
    EXPECT_FALSE(validate_value("hello worlds", t));      // collapses to "hello worlds" - not in enum
}

TEST(XsdFacet, TotalAndFractionDigits) {
    XsdSimpleType t{.base_type = XsdType::Decimal, .facets = {}, .name = "money"};
    t.facets.total_digits = 5;
    t.facets.fraction_digits = 2;
    EXPECT_TRUE(validate_value("123.45", t));
    EXPECT_FALSE(validate_value("1234.56", t));   // 5 digits total, but fraction 2 -> 1234.56 has 6 digits
    EXPECT_FALSE(validate_value("123.456", t));  // too many fraction digits
}

// ── Numeric Range Facets ───────────────────────────────────────────────

TEST(XsdFacet, IntegerRangeInclusive) {
    XsdSimpleType t{.base_type = XsdType::Int, .facets = {}, .name = "age"};
    t.facets.min_inclusive = "0";
    t.facets.max_inclusive = "120";
    EXPECT_TRUE(validate_value("25", t));
    EXPECT_TRUE(validate_value("0", t));
    EXPECT_TRUE(validate_value("120", t));
    EXPECT_FALSE(validate_value("-1", t));
    EXPECT_FALSE(validate_value("121", t));
}

TEST(XsdFacet, IntegerRangeExclusive) {
    XsdSimpleType t{.base_type = XsdType::Int, .facets = {}, .name = "percent"};
    t.facets.min_exclusive = "0";
    t.facets.max_exclusive = "100";
    EXPECT_TRUE(validate_value("50", t));
    EXPECT_FALSE(validate_value("0", t));
    EXPECT_FALSE(validate_value("100", t));
}

TEST(XsdFacet, DoubleRange) {
    XsdSimpleType t{.base_type = XsdType::Double, .facets = {}, .name = "ratio"};
    t.facets.min_inclusive = "0.0";
    t.facets.max_inclusive = "1.0";
    EXPECT_TRUE(validate_value("0.5", t));
    EXPECT_TRUE(validate_value("0", t));
    EXPECT_TRUE(validate_value("1.0", t));
    EXPECT_FALSE(validate_value("1.1", t));
    EXPECT_FALSE(validate_value("-0.1", t));
}

// ── Schema Document Validation ───────────────────────────────────────

TEST(XsdSchema, ValidDocument) {
    std::string xml = R"(<config>
        <port>8080</port>
        <enabled>true</enabled>
    </config>)";
    auto dom = parse_test_xml(xml);

    XsdSchema schema;
    schema.add_element("config", XsdType::String);
    schema.add_element("port", XsdType::UnsignedShort);
    schema.add_element("enabled", XsdType::Boolean);

    auto errors = schema.validate_document(dom);
    EXPECT_TRUE(errors.empty()) << errors[0];
}

TEST(XsdSchema, WrongElementType) {
    std::string xml = R"(<config><port>not-a-number</port></config>)";
    auto dom = parse_test_xml(xml);

    XsdSchema schema;
    schema.add_element("port", XsdType::UnsignedShort);

    auto errors = schema.validate_document(dom);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("invalid value"), std::string::npos);
}

TEST(XsdSchema, MissingRequiredAttribute) {
    std::string xml = R"(<config><item value="10"/></config>)";
    auto dom = parse_test_xml(xml);

    XsdSchema schema;
    schema.add_element("config", XsdType::String);
    schema.add_element("item", XsdType::String);
    schema.add_attribute("item", "id", XsdType::ID, true);
    schema.add_attribute("item", "value", XsdType::Int, true);

    auto errors = schema.validate_document(dom);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("missing required attribute 'id'"), std::string::npos);
}

TEST(XsdSchema, InvalidAttributeValue) {
    std::string xml = R"(<config><item id="item1" value="not-a-number"/></config>)";
    auto dom = parse_test_xml(xml);

    XsdSchema schema;
    schema.add_element("config", XsdType::String);
    schema.add_element("item", XsdType::String);
    schema.add_attribute("item", "id", XsdType::ID, true);
    schema.add_attribute("item", "value", XsdType::Int, true);

    auto errors = schema.validate_document(dom);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("attribute 'value' has invalid value"), std::string::npos);
}

TEST(XsdSchema, CustomSimpleTypeWithFacets) {
    std::string xml = R"(<config><status>active</status></config>)";
    auto dom = parse_test_xml(xml);

    XsdSimpleType status_type{
        .base_type = XsdType::Token,
        .facets = {},
        .name = "statusType"
    };
    status_type.facets.enumeration = {"active", "inactive", "pending"};

    XsdSchema schema;
    schema.add_element("status", status_type);

    auto errors = schema.validate_document(dom);
    EXPECT_TRUE(errors.empty());
}

TEST(XsdSchema, CustomSimpleTypeWithFacetsInvalid) {
    std::string xml = R"(<config><status>unknown</status></config>)";
    auto dom = parse_test_xml(xml);

    XsdSimpleType status_type{
        .base_type = XsdType::Token,
        .facets = {},
        .name = "statusType"
    };
    status_type.facets.enumeration = {"active", "inactive", "pending"};

    XsdSchema schema;
    schema.add_element("status", status_type);

    auto errors = schema.validate_document(dom);
    ASSERT_EQ(errors.size(), 1u);
}

TEST(XsdSchema, EmptyDocument) {
    std::string xml = "";
    auto dom = parse_test_xml(xml);

    XsdSchema schema;
    auto errors = schema.validate_document(dom);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("empty"), std::string::npos);
}
