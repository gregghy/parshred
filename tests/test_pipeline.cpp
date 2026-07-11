/// @file test_pipeline.cpp
/// @brief Unit tests for the chunked streaming and parallel pipeline.

#include <parshred/pipeline.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace parshred;

// ── Small chunk size to force boundary handling ──────────────────────

TEST(Pipeline, ChunkedBasic) {
    std::string xml = "<root><a>text</a><b>more</b></root>";
    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 16;  // very small to force chunking
    cfg.overlap = 8;

    ChunkedParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 3u);  // root, a, b
}

TEST(Pipeline, ChunkedNormalMode) {
    std::string xml = "<root><item id=\"1\">hello</item><item id=\"2\">world</item></root>";
    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 32;

    ChunkedParser<ParseMode::Normal> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 3u);  // root, item, item
    EXPECT_EQ(h.attributes, 2u);
}

TEST(Pipeline, ChunkedLargeChunk) {
    // Chunk size larger than data — should work like single parse
    std::string xml = "<root><a/><b>text</b></root>";
    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 1024 * 1024;

    ChunkedParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 3u);
}

TEST(Pipeline, ChunkedBytesProcessed) {
    std::string xml = "<root>some content</root>";
    CountingHandler h;

    ChunkedParser<ParseMode::Turbo> parser;
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(parser.bytes_processed(), xml.size());
}

TEST(Pipeline, ChunkedManyElements) {
    // Generate XML with many elements to span multiple chunks
    std::string xml = "<root>";
    for (int i = 0; i < 1000; ++i) {
        xml += "<item id=\"" + std::to_string(i) + "\">value" + std::to_string(i) + "</item>";
    }
    xml += "</root>";

    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 256;  // force many chunks

    ChunkedParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 1001u);  // root + 1000 items
}

TEST(Pipeline, ParallelBasic) {
    std::string xml = "<root><a>text</a><b>more</b></root>";
    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 16;

    ParallelParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 3u);
}

TEST(Pipeline, ParallelManyElements) {
    std::string xml = "<root>";
    for (int i = 0; i < 5000; ++i) {
        xml += "<item>" + std::to_string(i) + "</item>";
    }
    xml += "</root>";

    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 512;

    ParallelParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 5001u);  // root + 5000 items
}

TEST(Pipeline, ChunkedSelfClosingAtBoundary) {
    // Craft XML where a self-closing tag sits right at the chunk boundary
    std::string xml = "<r><a/><b/><c/><d/><e/><f/></r>";
    CountingHandler h;

    PipelineConfig cfg;
    cfg.chunk_size = 8;  // very small

    ChunkedParser<ParseMode::Turbo> parser(cfg);
    parser.parse(xml.data(), xml.size(), h);

    EXPECT_EQ(h.elements, 7u);  // r, a, b, c, d, e, f
}

TEST(Pipeline, ChunkedEmptyInput) {
    CountingHandler h;
    ChunkedParser<ParseMode::Turbo> parser;
    parser.parse("", 0, h);
    EXPECT_EQ(h.elements, 0u);
}
