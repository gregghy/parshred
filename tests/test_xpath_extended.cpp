/// @file test_xpath_extended.cpp
/// @brief Extended XPath 1.0 tests covering arithmetic, comparison, boolean,
///        string/number/boolean functions, and the union operator.

#include <parshred/xpath.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace parshred;
using namespace parshred::xpath;

// ── Helpers ──────────────────────────────────────────────────────────

static FastDom make_dom(const std::string& xml) {
    return fast_dom_parse<0>(xml.data(), xml.size());
}

// ══════════════════════════════════════════════════════════════════════
// 1. Arithmetic operators in predicates
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, ArithAdd_PriceGreaterThan) {
    // //book[price + 1 > 30]  — books whose price+1 exceeds 30
    std::string xml = R"(
        <bookstore>
            <book><price>29</price></book>
            <book><price>31</price></book>
            <book><price>20</price></book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//book[price + 1 > 30]");
    // price=29 → 30 is NOT > 30; price=31 → 32 > 30 ✓; price=20 → 21 is NOT > 30
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, ArithSub_PriceFiltered) {
    // books where price - 10 > 20 (i.e. price > 30)
    std::string xml = R"(
        <bookstore>
            <book><price>25</price></book>
            <book><price>35</price></book>
            <book><price>40</price></book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//book[price - 10 > 20]");
    // price=25 → 15 (no), 35 → 25 (yes), 40 → 30 (yes)
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, ArithMul_Filter) {
    // items where count * 2 > 10 (count > 5)
    std::string xml = R"(
        <root>
            <item><count>3</count></item>
            <item><count>6</count></item>
            <item><count>5</count></item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[count * 2 > 10]");
    // 3*2=6 (no), 6*2=12 (yes), 5*2=10 (no, not strictly greater)
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, ArithDiv_Filter) {
    // items where value div 2 > 5 (value > 10)
    std::string xml = R"(
        <root>
            <item><value>8</value></item>
            <item><value>12</value></item>
            <item><value>20</value></item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[value div 2 > 5]");
    // 8/2=4 (no), 12/2=6 (yes), 20/2=10 (yes)
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, ArithMod_Filter) {
    // items where index mod 2 = 0 (even)
    std::string xml = R"(
        <root>
            <item><index>1</index></item>
            <item><index>2</index></item>
            <item><index>3</index></item>
            <item><index>4</index></item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[index mod 2 = 0]");
    // index=2 and index=4 → 2 results
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 2. Comparison operators in predicates
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, CmpAttrNumericGt) {
    // //item[@count > 5]
    std::string xml = R"(
        <root>
            <item count="3"/>
            <item count="7"/>
            <item count="5"/>
            <item count="10"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@count > 5]");
    // count=7 and count=10 → 2 results
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, CmpAttrLt) {
    std::string xml = R"(<root><item v="2"/><item v="8"/><item v="5"/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@v < 5]");
    // v=2 (yes), v=8 (no), v=5 (no)
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, CmpAttrLte) {
    std::string xml = R"(<root><item v="2"/><item v="8"/><item v="5"/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@v <= 5]");
    // v=2 (yes), v=8 (no), v=5 (yes)
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, CmpAttrGte) {
    std::string xml = R"(<root><item v="2"/><item v="8"/><item v="5"/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@v >= 5]");
    // v=2 (no), v=8 (yes), v=5 (yes)
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, CmpAttrNeq) {
    std::string xml = R"(<root><item id="a"/><item id="b"/><item id="a"/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@id != 'a']");
    // only id="b" (1 result)
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, CmpAttrEqString) {
    std::string xml = R"(<root><item type="A"/><item type="B"/><item type="A"/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@type = 'A']");
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 3. Boolean operators: and, or
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, BoolAnd_CategoryAndPrice) {
    // //book[@category='fiction' and price < 40]
    std::string xml = R"(
        <bookstore>
            <book category="fiction"><price>29</price></book>
            <book category="fiction"><price>45</price></book>
            <book category="cooking"><price>25</price></book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//book[@category='fiction' and price < 40]");
    // Only the first book (fiction AND price < 40)
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, BoolOr_CategoryOrPrice) {
    // items where type='A' or value > 10
    std::string xml = R"(
        <root>
            <item type="A"><value>5</value></item>
            <item type="B"><value>15</value></item>
            <item type="B"><value>3</value></item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@type='A' or value > 10]");
    // item 1 (type=A), item 2 (value=15>10) — 2 results; item 3 (type=B, value=3) — no
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, BoolAndOr_Complex) {
    // (type='A' and value > 3) or (type='B' and value < 10)
    std::string xml = R"(
        <root>
            <item type="A"><value>5</value></item>
            <item type="A"><value>2</value></item>
            <item type="B"><value>8</value></item>
            <item type="B"><value>20</value></item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[(@type='A' and value > 3) or (@type='B' and value < 10)]");
    // item1 (A,5>3 yes), item2 (A,2>3 no), item3 (B,8<10 yes), item4 (B,20<10 no)
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 4. not() function
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, NotAttrHidden) {
    // //item[not(@hidden)]
    std::string xml = R"(
        <root>
            <item id="1"/>
            <item id="2" hidden="true"/>
            <item id="3"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[not(@hidden)]");
    // items 1 and 3 have no 'hidden' attribute → 2 results
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, NotContains) {
    // items where name does NOT contain "xml"
    std::string xml = R"(
        <root>
            <item name="xml-parser"/>
            <item name="json-lib"/>
            <item name="xpath-engine"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[not(contains(@name, 'xml'))]");
    // json-lib and xpath-engine (2 results)
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 5. Union operator (|)
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, UnionTitleAndAuthor) {
    // //title | //author
    std::string xml = R"(
        <bookstore>
            <book>
                <title>XPath Guide</title>
                <author>Alice</author>
            </book>
            <book>
                <title>XML Handbook</title>
                <author>Bob</author>
            </book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//title | //author");
    // 2 titles + 2 authors = 4 nodes total
    EXPECT_EQ(results.size(), 4u);
}

TEST(XPathExtended, UnionNoOverlap) {
    // a | b — non-overlapping node sets
    std::string xml = R"(<root><a>1</a><b>2</b><a>3</a></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//a | //b");
    EXPECT_EQ(results.size(), 3u);
}

TEST(XPathExtended, UnionWithPredicate) {
    // //item[@type='A'] | //item[@type='B']
    std::string xml = R"(
        <root>
            <item type="A"/>
            <item type="B"/>
            <item type="C"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@type='A'] | //item[@type='B']");
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 6. String functions
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncSubstring_Basic) {
    // substring('Hello', 2, 3) → "ell"
    // Tested via predicate: items where substring(name,1,3) = 'hel'
    // We'll just test through contains/substring in a predicate.
    std::string xml = R"(
        <root>
            <item name="hello"/>
            <item name="world"/>
            <item name="help"/>
        </root>
    )";
    auto dom = make_dom(xml);
    // Items where the first 3 chars of @name = "hel"
    auto results = evaluate(dom, "//item[substring(@name, 1, 3) = 'hel']");
    // "hello" → "hel" ✓, "world" → "wor" ✗, "help" → "hel" ✓
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, FuncSubstring_NoLength) {
    // substring('Hello', 3) → "llo" (from position 3 to end)
    std::string xml = R"(
        <root>
            <item name="hello"/>
            <item name="world"/>
        </root>
    )";
    auto dom = make_dom(xml);
    // substring(@name, 3) from 'hello' = 'llo', from 'world' = 'rld'
    auto results = evaluate(dom, "//item[substring(@name, 3) = 'llo']");
    EXPECT_EQ(results.size(), 1u);
    auto strings = evaluate_strings(dom, "//item[substring(@name, 3) = 'llo']/@name");
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(strings[0], "hello");
}

TEST(XPathExtended, FuncSubstringBefore) {
    // substring-before('Hello World', ' ') → "Hello"
    std::string xml = R"(
        <root>
            <item full="Hello World"/>
            <item full="NoSpace"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[substring-before(@full, ' ') = 'Hello']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncSubstringAfter) {
    // substring-after('Hello World', ' ') → "World"
    std::string xml = R"(
        <root>
            <item full="Hello World"/>
            <item full="Foo Bar"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[substring-after(@full, ' ') = 'World']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncConcat) {
    // items where concat(@first, '-', @last) = 'John-Doe'
    std::string xml = R"(
        <root>
            <item first="John" last="Doe"/>
            <item first="Jane" last="Smith"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[concat(@first, '-', @last) = 'John-Doe']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncNormalizeSpace) {
    // normalize-space("  hello   world  ") → "hello world"
    std::string xml = R"(
        <root>
            <item label="  hello   world  "/>
            <item label="clean"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[normalize-space(@label) = 'hello world']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncTranslate) {
    // translate('Hello', 'aeiou', 'AEIOU') → 'HEllO'
    // Items where translate(@name, 'abc', 'ABC') = 'ABCdef'
    std::string xml = R"(
        <root>
            <item name="abcdef"/>
            <item name="xyzdef"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[translate(@name, 'abc', 'ABC') = 'ABCdef']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncStringLength) {
    // string-length(@name) > 4
    std::string xml = R"(
        <root>
            <item name="hi"/>
            <item name="hello"/>
            <item name="world"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[string-length(@name) > 4]");
    // "hi"=2 (no), "hello"=5 (yes), "world"=5 (yes)
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 7. Number functions
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncNumber_StringToNum) {
    // number(@v) > 5 — should work even with string attributes
    std::string xml = R"(
        <root>
            <item v="3"/>
            <item v="7"/>
            <item v="10"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[number(@v) > 5]");
    // v=7 and v=10
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, FuncSum) {
    // sum(//price) — sums all price text values
    std::string xml = R"(
        <bookstore>
            <book><price>10</price></book>
            <book><price>20</price></book>
            <book><price>30</price></book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    // Evaluate sum via a predicate that's always true but uses sum for a check
    // We can't directly call sum() on its own through the existing API,
    // but we can test it via the predicate evaluator.
    // Let's test via a context item predicate with a known result.
    // [sum(//price) > 50] matches if sum > 50 (= 60)
    auto results = evaluate(dom, "//bookstore[sum(//price) > 50]");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncFloor) {
    // floor(@v) = 3 for values 3.7, 3.1
    std::string xml = R"(
        <root>
            <item v="3.7"/>
            <item v="3.1"/>
            <item v="4.0"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[floor(@v) = 3]");
    // floor(3.7)=3, floor(3.1)=3, floor(4.0)=4 → 2 results
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, FuncCeiling) {
    // ceiling(@v) = 4 for values 3.7, 3.1
    std::string xml = R"(
        <root>
            <item v="3.7"/>
            <item v="3.1"/>
            <item v="4.0"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[ceiling(@v) = 4]");
    // ceil(3.7)=4, ceil(3.1)=4, ceil(4.0)=4 → 3 results
    EXPECT_EQ(results.size(), 3u);
}

TEST(XPathExtended, FuncRound) {
    // round(@v): 3.4→3, 3.5→4, 3.6→4
    std::string xml = R"(
        <root>
            <item v="3.4"/>
            <item v="3.5"/>
            <item v="3.6"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results_3 = evaluate(dom, "//item[round(@v) = 3]");
    auto results_4 = evaluate(dom, "//item[round(@v) = 4]");
    // round(3.4)=3 (1 result), round(3.5)=4 and round(3.6)=4 (2 results)
    EXPECT_EQ(results_3.size(), 1u);
    EXPECT_EQ(results_4.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 8. Boolean functions: true(), false(), not()
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncTrue) {
    // [true()] selects all items
    std::string xml = R"(<root><item/><item/><item/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[true()]");
    EXPECT_EQ(results.size(), 3u);
}

TEST(XPathExtended, FuncFalse) {
    // [false()] selects nothing
    std::string xml = R"(<root><item/><item/><item/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[false()]");
    EXPECT_EQ(results.size(), 0u);
}

TEST(XPathExtended, FuncNotBool) {
    // [not(false())] selects all
    std::string xml = R"(<root><item/><item/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[not(false())]");
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 9. Node functions: local-name(), name()
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncLocalName) {
    // local-name() of children — filter by local name
    std::string xml = R"(
        <root>
            <ns:item xmlns:ns="http://example.com"/>
            <other/>
        </root>
    )";
    auto dom = make_dom(xml);
    // name() of first child should be "ns:item" but local-name should strip prefix
    auto results = evaluate(dom, "/root/*[local-name() = 'item']");
    EXPECT_EQ(results.size(), 1u);
}

TEST(XPathExtended, FuncName) {
    // name() returns full name including prefix
    std::string xml = R"(<root><book/><magazine/></root>)";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "/root/*[name() = 'book']");
    EXPECT_EQ(results.size(), 1u);
}

// ══════════════════════════════════════════════════════════════════════
// 10. Complex multi-feature tests
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, Complex_ContainsAndCategory) {
    // //book[contains(title, 'XML') and @category='tech']
    std::string xml = R"(
        <bookstore>
            <book category="tech">
                <title>XML Processing Guide</title>
                <price>39.99</price>
            </book>
            <book category="fiction">
                <title>XML in Space</title>
                <price>19.99</price>
            </book>
            <book category="tech">
                <title>JSON Handbook</title>
                <price>29.99</price>
            </book>
        </bookstore>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//book[contains(title, 'XML') and @category='tech']");
    // Only "XML Processing Guide" in category "tech" (1 result)
    EXPECT_EQ(results.size(), 1u);
    auto strings = evaluate_strings(dom, "//book[contains(title, 'XML') and @category='tech']/title");
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(strings[0], "XML Processing Guide");
}

TEST(XPathExtended, Complex_ArithAndBool) {
    // //product[@price > 10 and @price < 50 and @stock > 0]
    std::string xml = R"(
        <catalog>
            <product name="A" price="5" stock="10"/>
            <product name="B" price="25" stock="5"/>
            <product name="C" price="100" stock="3"/>
            <product name="D" price="30" stock="0"/>
        </catalog>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//product[@price > 10 and @price < 50 and @stock > 0]");
    // Only product B (price=25, stock=5) matches
    EXPECT_EQ(results.size(), 1u);
    auto strings = evaluate_strings(dom, "//product[@price > 10 and @price < 50 and @stock > 0]/@name");
    ASSERT_EQ(strings.size(), 1u);
    EXPECT_EQ(strings[0], "B");
}

TEST(XPathExtended, Complex_UnionAndPredicate) {
    // (//chapter | //appendix)[@important='yes']
    std::string xml = R"(
        <book>
            <chapter important="yes">Intro</chapter>
            <chapter important="no">Background</chapter>
            <appendix important="yes">Index</appendix>
            <appendix important="no">Glossary</appendix>
        </book>
    )";
    auto dom = make_dom(xml);
    // Union then filter: test each branch
    auto chapters = evaluate(dom, "//chapter[@important='yes']");
    auto appendices = evaluate(dom, "//appendix[@important='yes']");
    EXPECT_EQ(chapters.size(), 1u);
    EXPECT_EQ(appendices.size(), 1u);

    // Using union
    auto union_results = evaluate(dom, "//chapter[@important='yes'] | //appendix[@important='yes']");
    EXPECT_EQ(union_results.size(), 2u);
}

TEST(XPathExtended, Complex_SubstringAndNot) {
    // //item[starts-with(@code, 'A') and not(contains(@code, 'X'))]
    std::string xml = R"(
        <root>
            <item code="A100"/>
            <item code="AX50"/>
            <item code="B200"/>
            <item code="A300"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[starts-with(@code, 'A') and not(contains(@code, 'X'))]");
    // A100 (yes), AX50 (no, has X), B200 (no, not A), A300 (yes)
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, Complex_PositionAndArith) {
    // //item[position() * 2 <= 4] — first 2 items (positions 1 and 2)
    std::string xml = R"(
        <root>
            <item>a</item>
            <item>b</item>
            <item>c</item>
            <item>d</item>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "/root/item[position() * 2 <= 4]");
    // pos=1: 1*2=2<=4 yes, pos=2: 2*2=4<=4 yes, pos=3: 3*2=6 no, pos=4: 4*2=8 no
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, Complex_BookstoreFullQuery) {
    // Full bookstore with multiple features
    std::string xml = R"(
        <bookstore>
            <book category="cooking">
                <title>Italian Cooking</title>
                <price>30.00</price>
            </book>
            <book category="fiction">
                <title>Harry Potter</title>
                <price>29.99</price>
            </book>
            <book category="fiction">
                <title>Lord of the Rings</title>
                <price>39.95</price>
            </book>
            <book category="tech">
                <title>XPath Guide</title>
                <price>49.00</price>
            </book>
        </bookstore>
    )";
    auto dom = make_dom(xml);

    // All books where price > 35 OR category = 'cooking'
    auto results1 = evaluate(dom, "//book[price > 35 or @category='cooking']");
    // Italian Cooking (cooking, price=30), Lord of the Rings (price=39.95), XPath Guide (price=49)
    EXPECT_EQ(results1.size(), 3u);

    // Fiction books under 35
    auto results2 = evaluate(dom, "//book[@category='fiction' and price < 35]");
    EXPECT_EQ(results2.size(), 1u);  // Harry Potter (29.99)

    // All titles and prices (union)
    auto results3 = evaluate(dom, "//title | //price");
    EXPECT_EQ(results3.size(), 8u);  // 4 titles + 4 prices
}

// ══════════════════════════════════════════════════════════════════════
// 11. contains() and starts-with() (already supported, verify integration)
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncContainsInPredicate) {
    std::string xml = R"(
        <root>
            <item name="foobar"/>
            <item name="bazqux"/>
            <item name="fooqwe"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[contains(@name, 'foo')]");
    // foobar and fooqwe
    EXPECT_EQ(results.size(), 2u);
}

TEST(XPathExtended, FuncStartsWithInPredicate) {
    std::string xml = R"(
        <root>
            <item name="prefix_a"/>
            <item name="prefix_b"/>
            <item name="other"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[starts-with(@name, 'prefix')]");
    EXPECT_EQ(results.size(), 2u);
}

// ══════════════════════════════════════════════════════════════════════
// 12. count() function in predicates
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, FuncCount_InPredicate) {
    // sections with more than 2 subsection children
    std::string xml = R"(
        <doc>
            <section>
                <sub/><sub/>
            </section>
            <section>
                <sub/><sub/><sub/>
            </section>
            <section>
                <sub/>
            </section>
        </doc>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//section[count(sub) > 2]");
    // Only section 2 has 3 subs
    EXPECT_EQ(results.size(), 1u);
}

// ══════════════════════════════════════════════════════════════════════
// 13. Chained predicates
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, ChainedPredicates) {
    // //item[@type='A'][@value > 5] — two predicates applied sequentially
    std::string xml = R"(
        <root>
            <item type="A" value="3"/>
            <item type="A" value="7"/>
            <item type="B" value="10"/>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//item[@type='A'][@value > 5]");
    // Only item with type=A AND value=7
    EXPECT_EQ(results.size(), 1u);
}

// ══════════════════════════════════════════════════════════════════════
// 14. XPathValue arithmetic (stand-alone verification via predicate)
// ══════════════════════════════════════════════════════════════════════

TEST(XPathExtended, ArithPrecedence_MulBeforeAdd) {
    // price[. = 2 + 3 * 4]  (= 2 + 12 = 14, not (2+3)*4=20)
    std::string xml = R"(
        <root>
            <price>14</price>
            <price>20</price>
        </root>
    )";
    auto dom = make_dom(xml);
    auto results = evaluate(dom, "//price[. = 2 + 3 * 4]");
    // 2 + 3*4 = 14 → matches first price
    EXPECT_EQ(results.size(), 1u);
    auto strs = evaluate_strings(dom, "//price[. = 2 + 3 * 4]");
    ASSERT_EQ(strs.size(), 1u);
    EXPECT_EQ(strs[0], "14");
}
