/// @file scanner_scalar.cpp
/// @brief Scalar (non-SIMD) structural character scanner fallback.

#include <parshred/simd_scanner.hpp>

namespace parshred::detail {

/// Checks if `c` is an XML structural character.
static inline bool is_structural(char c) noexcept {
    switch (c) {
        case '<': case '>': case '/': case '"': case '\'': case '=': case '&':
            return true;
        default:
            return false;
    }
}

void scan_scalar(const char* data, size_t len,
                 std::vector<uint32_t>& positions,
                 std::vector<uint8_t>& chars)
{
    // Track quote state: 0 = not in quotes, '"' or '\'' = in that quote type
    char in_quote = 0;

    for (size_t i = 0; i < len; ++i) {
        char c = data[i];

        if (in_quote) {
            // Only look for the matching close quote
            if (c == in_quote) {
                // Emit the closing quote as structural
                positions.push_back(static_cast<uint32_t>(i));
                chars.push_back(static_cast<uint8_t>(c));
                in_quote = 0;
            }
            // Everything else inside quotes is skipped
            continue;
        }

        if (is_structural(c)) {
            positions.push_back(static_cast<uint32_t>(i));
            chars.push_back(static_cast<uint8_t>(c));

            if (c == '"' || c == '\'') {
                in_quote = c;
            }
        }
    }
}

} // namespace parshred::detail
