#pragma once
/// @file tokenizer.hpp
/// @brief XML tokenizer — converts structural index + raw input into a token stream.

#include <parshred/common.hpp>
#include <parshred/simd_scanner.hpp>
#include <span>
#include <vector>

namespace parshred {

/// Walks the structural index and produces a flat token stream.
///
/// All Token::text values are zero-copy string_views into the original input.
/// The tokenizer validates basic well-formedness but does NOT expand entities
/// or check tag matching — that is the parser's job.
class Tokenizer {
public:
    Tokenizer() = default;

    /// Tokenize the input using the provided structural index.
    /// @throws ParseError on malformed XML.
    void tokenize(std::span<const char> input, const StructuralIndex& index);

    /// Tokenize directly from raw input (will run SIMD scan internally).
    void tokenize(std::span<const char> input);

    /// Access the resulting token stream.
    [[nodiscard]] const std::vector<Token>& tokens() const noexcept { return tokens_; }
    [[nodiscard]] std::vector<Token>& tokens() noexcept { return tokens_; }

    /// Number of tokens produced.
    [[nodiscard]] size_t size() const noexcept { return tokens_.size(); }

    /// Clear the token stream (for reuse).
    void clear() noexcept { tokens_.clear(); }

    // Character classification (public for use by other components)
    [[nodiscard]] static bool is_name_start_char(char c) noexcept;
    [[nodiscard]] static bool is_name_char(char c) noexcept;
    [[nodiscard]] static bool is_whitespace(char c) noexcept;

private:
    std::vector<Token> tokens_;
    std::span<const char> input_;

    // Internal helpers
    void process_tag(size_t start, size_t end);
    void process_text(size_t start, size_t end);
    void process_comment(size_t start);
    void process_cdata(size_t start);
    void process_pi(size_t start);
    // DOCTYPE is handled inline in tokenize() to properly track the end position

    size_t skip_whitespace(size_t pos) const noexcept;
    size_t read_name(size_t pos) const noexcept;
};

} // namespace parshred
