/// @file sax_parser.cpp
/// @brief SAX streaming parser implementation.

#include <parshred/sax_parser.hpp>
#include <parshred/tokenizer.hpp>
#include <parshred/simd_scanner.hpp>
#include <parshred/mmap_reader.hpp>

#include <algorithm>
#include <cstring>

namespace parshred {

// ── Entity expansion ──────────────────────────────────────────────────

std::string SaxParser::expand_entities(std::string_view text) {
    // Fast path: no '&' means no entities
    if (text.find('&') == std::string_view::npos) {
        return std::string(text);
    }

    std::string result;
    result.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            size_t semi = text.find(';', i + 1);
            if (semi != std::string_view::npos) {
                std::string_view entity = text.substr(i + 1, semi - i - 1);

                if (++entity_expansion_count_ > max_entity_expansions_) {
                    throw SecurityError("Entity expansion limit exceeded", i);
                }

                if (entity == "lt")        { result += '<'; }
                else if (entity == "gt")    { result += '>'; }
                else if (entity == "amp")   { result += '&'; }
                else if (entity == "apos")  { result += '\''; }
                else if (entity == "quot")  { result += '"'; }
                else if (entity.size() > 1 && entity[0] == '#') {
                    // Numeric character reference
                    unsigned long codepoint = 0;
                    if (entity[1] == 'x' || entity[1] == 'X') {
                        // Hex: &#xHH;
                        for (size_t j = 2; j < entity.size(); ++j) {
                            char c = entity[j];
                            codepoint *= 16;
                            if (c >= '0' && c <= '9') codepoint += c - '0';
                            else if (c >= 'a' && c <= 'f') codepoint += 10 + c - 'a';
                            else if (c >= 'A' && c <= 'F') codepoint += 10 + c - 'A';
                        }
                    } else {
                        // Decimal: &#DD;
                        for (size_t j = 1; j < entity.size(); ++j) {
                            codepoint = codepoint * 10 + (entity[j] - '0');
                        }
                    }
                    // Encode as UTF-8
                    if (codepoint < 0x80) {
                        result += static_cast<char>(codepoint);
                    } else if (codepoint < 0x800) {
                        result += static_cast<char>(0xC0 | (codepoint >> 6));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else if (codepoint < 0x10000) {
                        result += static_cast<char>(0xE0 | (codepoint >> 12));
                        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else if (codepoint < 0x110000) {
                        result += static_cast<char>(0xF0 | (codepoint >> 18));
                        result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                } else {
                    // Unknown entity — pass through as-is
                    result += text.substr(i, semi - i + 1);
                }
                i = semi + 1;
                continue;
            }
        }
        result += text[i];
        ++i;
    }
    return result;
}

// ── Parsing ───────────────────────────────────────────────────────────

void SaxParser::parse(std::span<const char> input) {
    do_parse(input);
}

void SaxParser::parse_file(const std::string& path) {
    MmapReader reader;
    reader.open(path);
    do_parse(reader.data());
}

void SaxParser::parse_string(std::string_view str) {
    do_parse({str.data(), str.size()});
}

void SaxParser::do_parse(std::span<const char> input) {
    // Reset state
    current_depth_ = 0;
    entity_expansion_count_ = 0;
    stats_.bytes_parsed = input.size();

    // Tokenize
    Tokenizer tokenizer;
    tokenizer.tokenize(input);

    const auto& tokens = tokenizer.tokens();

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];

        switch (tok.type) {
            case TokenType::StartTag: {
                if (++current_depth_ > max_depth_) {
                    throw SecurityError("Maximum nesting depth exceeded", tok.offset);
                }

                // Collect attributes
                attrs_scratch_.clear();
                std::string_view attr_name;
                size_t j = i + 1;
                while (j < tokens.size()) {
                    if (tokens[j].type == TokenType::AttributeName) {
                        attr_name = tokens[j].text;
                        ++j;
                        if (j < tokens.size() && tokens[j].type == TokenType::AttributeValue) {
                            if (attrs_scratch_.size() >= max_attribute_count_) {
                                throw SecurityError("Maximum attribute count exceeded",
                                                    tokens[j].offset);
                            }
                            attrs_scratch_.push_back({attr_name, tokens[j].text});
                            ++j;
                        } else {
                            // Attribute without value (invalid in XML, but tolerate)
                            attrs_scratch_.push_back({attr_name, {}});
                        }
                    } else {
                        break;
                    }
                }
                i = j - 1; // advance past consumed tokens

                ++stats_.elements;
                stats_.attributes += attrs_scratch_.size();

                if (start_element_cb_) {
                    start_element_cb_(tok.text,
                        std::span<const Attribute>(attrs_scratch_));
                }
                break;
            }

            case TokenType::SelfClosingTag: {
                // Self-closing tags fire both start and end
                attrs_scratch_.clear();
                std::string_view attr_name;
                size_t j = i + 1;
                while (j < tokens.size()) {
                    if (tokens[j].type == TokenType::AttributeName) {
                        attr_name = tokens[j].text;
                        ++j;
                        if (j < tokens.size() && tokens[j].type == TokenType::AttributeValue) {
                            if (attrs_scratch_.size() >= max_attribute_count_) {
                                throw SecurityError("Maximum attribute count exceeded",
                                                    tokens[j].offset);
                            }
                            attrs_scratch_.push_back({attr_name, tokens[j].text});
                            ++j;
                        } else {
                            attrs_scratch_.push_back({attr_name, {}});
                        }
                    } else {
                        break;
                    }
                }
                i = j - 1;

                ++stats_.elements;
                stats_.attributes += attrs_scratch_.size();

                if (start_element_cb_) {
                    start_element_cb_(tok.text,
                        std::span<const Attribute>(attrs_scratch_));
                }
                if (end_element_cb_) {
                    end_element_cb_(tok.text);
                }
                break;
            }

            case TokenType::EndTag: {
                if (current_depth_ == 0) {
                    throw ParseError("Unexpected end tag", tok.offset);
                }
                --current_depth_;

                if (end_element_cb_) {
                    end_element_cb_(tok.text);
                }
                break;
            }

            case TokenType::Text: {
                ++stats_.text_nodes;
                if (text_cb_) {
                    text_cb_(tok.text);
                }
                break;
            }

            case TokenType::EntityRef: {
                // Expand and emit as text
                ++stats_.text_nodes;
                if (text_cb_) {
                    std::string expanded = expand_entities(tok.text);
                    text_cb_(expanded);
                }
                break;
            }

            case TokenType::Comment: {
                ++stats_.comments;
                if (comment_cb_) {
                    comment_cb_(tok.text);
                }
                break;
            }

            case TokenType::CData: {
                ++stats_.cdata_nodes;
                if (cdata_cb_) {
                    cdata_cb_(tok.text);
                }
                break;
            }

            case TokenType::ProcessingInstruction: {
                if (pi_cb_) {
                    // Extract target and data from the PI text
                    // PI text is "<?target data?>"
                    std::string_view content = tok.text;
                    if (content.size() > 4) {
                        content = content.substr(2, content.size() - 4); // strip <? and ?>
                        size_t space = 0;
                        while (space < content.size() && !Tokenizer::is_whitespace(content[space]))
                            ++space;
                        std::string_view target = content.substr(0, space);
                        std::string_view pi_data;
                        if (space < content.size()) {
                            size_t data_start = space;
                            while (data_start < content.size() &&
                                   Tokenizer::is_whitespace(content[data_start]))
                                ++data_start;
                            pi_data = content.substr(data_start);
                        }
                        pi_cb_(target, pi_data);
                    }
                }
                break;
            }

            case TokenType::XmlDeclaration: {
                if (xml_decl_cb_) {
                    // Parse version, encoding, standalone from the declaration
                    std::string_view content = tok.text;
                    auto extract_attr = [&](const char* name) -> std::string_view {
                        auto pos = content.find(name);
                        if (pos == std::string_view::npos) return {};
                        pos = content.find('=', pos);
                        if (pos == std::string_view::npos) return {};
                        ++pos;
                        while (pos < content.size() &&
                               (content[pos] == ' ' || content[pos] == '"' || content[pos] == '\''))
                            ++pos;
                        size_t start = pos;
                        while (pos < content.size() && content[pos] != '"' && content[pos] != '\'')
                            ++pos;
                        return content.substr(start, pos - start);
                    };
                    xml_decl_cb_(extract_attr("version"),
                                 extract_attr("encoding"),
                                 extract_attr("standalone"));
                }
                break;
            }

            case TokenType::DocType:
            case TokenType::EndOfInput:
                break;

            default:
                break;
        }
    }
}

} // namespace parshred
