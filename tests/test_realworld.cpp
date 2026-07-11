/// @file test_realworld.cpp
/// @brief Real-world XML parsing tests: SVG, RSS, XHTML, Maven POM,
///        SOAP, and Android layout.

#include <parshred/dom_fast.hpp>
#include <parshred/xpath.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace parshred;
using namespace parshred::xpath;

// ════════════════════════════════════════════════════════════════════════
// SVG (Scalable Vector Graphics)
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, SVG) {
    std::string xml = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     viewBox="0 0 100 100" width="200" height="200">
  <defs>
    <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:rgb(255,255,0);stop-opacity:1"/>
      <stop offset="100%" style="stop-color:rgb(255,0,0);stop-opacity:1"/>
    </linearGradient>
  </defs>
  <circle cx="50" cy="50" r="40" fill="url(#grad1)" stroke="black" stroke-width="2"/>
  <text x="50" y="55" text-anchor="middle" font-size="12">SVG</text>
</svg>
)svg";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    // Root element is "svg" and declares the SVG namespace.
    EXPECT_EQ(dom.name(root), "svg");
    EXPECT_EQ(dom.attr(root, "xmlns"), "http://www.w3.org/2000/svg");
    EXPECT_EQ(dom.attr(root, "xmlns:xlink"), "http://www.w3.org/1999/xlink");
    EXPECT_EQ(dom.attr(root, "viewBox"), "0 0 100 100");

    // Count top-level element children of svg (defs + circle + text = 3).
    size_t child_count = 0;
    for (const FastNode* child = dom.first_child(root); child; child = dom.next_sibling(*child)) {
        if (child->type == 1) ++child_count;
    }
    EXPECT_EQ(child_count, 3u);

    // XPath finds the circle and its attributes.
    EXPECT_EQ(evaluate_count(dom, "//circle"), 1u);
    EXPECT_EQ(evaluate_string(dom, "//circle/@cx"), "50");
    EXPECT_EQ(evaluate_string(dom, "//circle/@cy"), "50");
    EXPECT_EQ(evaluate_string(dom, "//circle/@r"), "40");
    EXPECT_EQ(evaluate_string(dom, "//circle/@fill"), "url(#grad1)");
    EXPECT_EQ(evaluate_string(dom, "//text"), "SVG");
}

// ════════════════════════════════════════════════════════════════════════
// RSS 2.0 Feed
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, RSS) {
    std::string xml = R"rss(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">
  <channel>
    <title>Tech News</title>
    <link>https://example.com</link>
    <description>Latest tech news</description>
    <atom:link href="https://example.com/feed" rel="self" type="application/rss+xml"/>
    <item>
      <title>New XML Parser Released</title>
      <link>https://example.com/parshred</link>
      <description>Parshred beats RapidXML</description>
      <pubDate>Mon, 14 Jul 2025 10:00:00 GMT</pubDate>
      <guid>https://example.com/parshred</guid>
    </item>
    <item>
      <title>SIMD Optimization Guide</title>
      <link>https://example.com/simd</link>
      <description>How to use AVX2 for parsing</description>
      <pubDate>Sun, 13 Jul 2025 08:00:00 GMT</pubDate>
      <guid>https://example.com/simd</guid>
    </item>
  </channel>
</rss>
)rss";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    EXPECT_EQ(dom.name(root), "rss");
    EXPECT_EQ(dom.attr(root, "version"), "2.0");

    // Channel title via XPath.
    EXPECT_EQ(evaluate_string(dom, "/rss/channel/title"), "Tech News");
    EXPECT_EQ(evaluate_string(dom, "/rss/channel/link"), "https://example.com");

    // Count items and extract their titles.
    EXPECT_EQ(evaluate_count(dom, "//item"), 2u);
    auto titles = evaluate_strings(dom, "//item/title");
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_EQ(titles[0], "New XML Parser Released");
    EXPECT_EQ(titles[1], "SIMD Optimization Guide");

    // Verify atom:link namespace declaration and attributes.
    auto atom_links = evaluate(dom, "//atom:link");
    ASSERT_EQ(atom_links.size(), 1u);
    EXPECT_EQ(evaluate_string(dom, "//atom:link/@href"), "https://example.com/feed");
    EXPECT_EQ(evaluate_string(dom, "//atom:link/@rel"), "self");
}

// ════════════════════════════════════════════════════════════════════════
// XHTML Document
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, XHTML) {
    std::string xml = R"xhtml(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head>
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8"/>
  <title>Test Page</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>
  <div id="content" class="main">
    <h1>Hello World</h1>
    <p>This is a <strong>test</strong> paragraph with <a href="https://example.com">a link</a>.</p>
    <ul>
      <li>Item 1</li>
      <li>Item 2</li>
      <li>Item 3</li>
    </ul>
  </div>
</body>
</html>
)xhtml";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    EXPECT_EQ(dom.name(root), "html");
    EXPECT_EQ(dom.attr(root, "xmlns"), "http://www.w3.org/1999/xhtml");
    EXPECT_EQ(dom.attr(root, "xml:lang"), "en");

    // Title text via XPath.
    EXPECT_EQ(evaluate_string(dom, "//title"), "Test Page");

    // Count li elements.
    EXPECT_EQ(evaluate_count(dom, "//li"), 3u);

    // Find the content div by id attribute.
    auto divs = evaluate(dom, "//div[@id='content']");
    ASSERT_EQ(divs.size(), 1u);
    const auto& div = dom.nodes[divs[0]];
    EXPECT_EQ(dom.name(div), "div");
    EXPECT_EQ(dom.attr(div, "class"), "main");

    // Mixed text content of the paragraph.
    EXPECT_EQ(evaluate_string(dom, "//p"), "This is a test paragraph with a link.");
}

// ════════════════════════════════════════════════════════════════════════
// Maven POM
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, MavenPOM) {
    std::string xml = R"pom(<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
  <modelVersion>4.0.0</modelVersion>
  <groupId>com.example</groupId>
  <artifactId>my-app</artifactId>
  <version>1.0-SNAPSHOT</version>
  <packaging>jar</packaging>
  <dependencies>
    <dependency>
      <groupId>junit</groupId>
      <artifactId>junit</artifactId>
      <version>4.13.2</version>
      <scope>test</scope>
    </dependency>
    <dependency>
      <groupId>org.apache.commons</groupId>
      <artifactId>commons-lang3</artifactId>
      <version>3.12.0</version>
    </dependency>
  </dependencies>
</project>
)pom";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    EXPECT_EQ(dom.name(root), "project");
    EXPECT_EQ(dom.attr(root, "xmlns"), "http://maven.apache.org/POM/4.0.0");

    // Count dependencies.
    EXPECT_EQ(evaluate_count(dom, "//dependency"), 2u);

    // Get groupId/artifactId of the first dependency.
    EXPECT_EQ(evaluate_string(dom, "//dependency[1]/groupId"), "junit");
    EXPECT_EQ(evaluate_string(dom, "//dependency[1]/artifactId"), "junit");
    EXPECT_EQ(evaluate_string(dom, "//dependency[1]/version"), "4.13.2");
    EXPECT_EQ(evaluate_string(dom, "//dependency[1]/scope"), "test");

    EXPECT_EQ(evaluate_string(dom, "//dependency[2]/groupId"), "org.apache.commons");
    EXPECT_EQ(evaluate_string(dom, "//dependency[2]/artifactId"), "commons-lang3");

    // Find test-scoped dependencies.
    auto test_deps = evaluate(dom, "//dependency[scope='test']");
    ASSERT_EQ(test_deps.size(), 1u);
    EXPECT_EQ(evaluate_string(dom, "//dependency[scope='test']/artifactId"), "junit");
}

// ════════════════════════════════════════════════════════════════════════
// SOAP Envelope
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, SOAPEnvelope) {
    std::string xml = R"soap(<?xml version="1.0" encoding="UTF-8"?>
<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"
               xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
               xmlns:xsd="http://www.w3.org/2001/XMLSchema">
  <soap:Header>
    <auth xmlns="http://example.com/auth">
      <token>abc123def456</token>
    </auth>
  </soap:Header>
  <soap:Body>
    <GetStockPrice xmlns="http://example.com/stocks">
      <StockName>GOOG</StockName>
    </GetStockPrice>
  </soap:Body>
</soap:Envelope>
)soap";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    // Verify namespaced root.
    EXPECT_EQ(dom.name(root), "soap:Envelope");
    EXPECT_EQ(dom.attr(root, "xmlns:soap"), "http://schemas.xmlsoap.org/soap/envelope/");

    // Find Body content via XPath.
    auto bodies = evaluate(dom, "//soap:Body");
    ASSERT_EQ(bodies.size(), 1u);

    // Get the stock name text.
    EXPECT_EQ(evaluate_string(dom, "//StockName"), "GOOG");

    // Verify the authentication token.
    EXPECT_EQ(evaluate_string(dom, "//auth/token"), "abc123def456");
}

// ════════════════════════════════════════════════════════════════════════
// Android Layout XML
// ════════════════════════════════════════════════════════════════════════

TEST(RealWorld, AndroidLayout) {
    std::string xml = R"layout(<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:app="http://schemas.android.com/apk/res-auto"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">
    <TextView
        android:id="@+id/title"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="Hello World"/>
    <Button
        android:id="@+id/button"
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:text="Click Me"
        app:cornerRadius="8dp"/>
</LinearLayout>
)layout";

    auto dom = fast_dom_parse<0>(xml.data(), xml.size());
    const auto& root = dom.root();

    EXPECT_EQ(dom.name(root), "LinearLayout");

    // Attribute access with namespace prefix.
    EXPECT_EQ(dom.attr(root, "android:layout_width"), "match_parent");
    EXPECT_EQ(dom.attr(root, "android:layout_height"), "match_parent");
    EXPECT_EQ(dom.attr(root, "android:orientation"), "vertical");
    EXPECT_EQ(dom.attr(root, "xmlns:android"), "http://schemas.android.com/apk/res/android");

    // Count child views (TextView + Button = 2).
    EXPECT_EQ(evaluate_count(dom, "/LinearLayout/*"), 2u);

    // Verify child view names and attributes via XPath.
    auto children = evaluate(dom, "/LinearLayout/*");
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(dom.name(dom.nodes[children[0]]), "TextView");
    EXPECT_EQ(dom.name(dom.nodes[children[1]]), "Button");

    EXPECT_EQ(evaluate_string(dom, "/LinearLayout/TextView/@android:text"), "Hello World");
    EXPECT_EQ(evaluate_string(dom, "/LinearLayout/Button/@android:id"), "@+id/button");
    EXPECT_EQ(evaluate_string(dom, "/LinearLayout/Button/@app:cornerRadius"), "8dp");
}
