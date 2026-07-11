#pragma once
/// @file parshred.hpp
/// @brief Umbrella header — includes all public parshred headers.

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------
#include <parshred/common.hpp>
#include <parshred/arena.hpp>
#include <parshred/lookup_tables.hpp>

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------
#include <parshred/platform.hpp>
#include <parshred/simd_utils.hpp>
#include <parshred/simd_neon.hpp>
#include <parshred/simd_scanner.hpp>

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------
#include <parshred/error.hpp>
#include <parshred/encoding.hpp>

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
#include <parshred/tokenizer.hpp>
#include <parshred/sax_parser.hpp>
#include <parshred/fast_sax.hpp>
#include <parshred/fast_sax_impl.hpp>

// ---------------------------------------------------------------------------
// DOM
// ---------------------------------------------------------------------------
#include <parshred/dom.hpp>
#include <parshred/dom_pool.hpp>
#include <parshred/dom_fast.hpp>
#include <parshred/dom_parser.hpp>
#include <parshred/dom_parser_impl.hpp>

// ---------------------------------------------------------------------------
// Features
// ---------------------------------------------------------------------------
#include <parshred/namespace.hpp>
#include <parshred/xpath.hpp>
#include <parshred/dtd.hpp>
#include <parshred/xsd.hpp>

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------
#include <parshred/mmap_reader.hpp>
#include <parshred/pipeline.hpp>
#include <parshred/pipeline_impl.hpp>
#include <parshred/pull_parser.hpp>

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
#include <parshred/writer.hpp>
