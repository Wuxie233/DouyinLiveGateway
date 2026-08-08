#include "douyin/Json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace douyin::json {
namespace {

bool IsValidUtf8(std::string_view input)
{
    for (std::size_t index = 0; index < input.size();) {
        const unsigned char first = static_cast<unsigned char>(input[index++]);
        if (first <= 0x7F) continue;

        std::uint32_t codepoint = 0;
        std::size_t continuation_count = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            codepoint = first & 0x1F;
            continuation_count = 1;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codepoint = first & 0x0F;
            continuation_count = 2;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codepoint = first & 0x07;
            continuation_count = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (index + continuation_count > input.size()) return false;
        for (std::size_t count = 0; count < continuation_count; ++count) {
            const unsigned char next = static_cast<unsigned char>(input[index++]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF
            || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
    }
    return true;
}

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    bool Run(Value& result, std::string& error)
    {
        SkipWhitespace();
        if (!ParseValue(result)) {
            error = error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            error = "trailing characters";
            return false;
        }
        return true;
    }

private:
    bool ParseValue(Value& result)
    {
        if (depth_ >= 64) return Fail("maximum nesting depth exceeded");
        ++depth_;
        const bool parsed = ParseValueInner(result);
        --depth_;
        return parsed;
    }

    bool ParseValueInner(Value& result)
    {
        if (position_ >= input_.size()) {
            return Fail("unexpected end of input");
        }
        switch (input_[position_]) {
        case 'n': return ParseLiteral("null", nullptr, result);
        case 't': return ParseLiteral("true", true, result);
        case 'f': return ParseLiteral("false", false, result);
        case '"': {
            std::string value;
            if (!ParseString(value)) return false;
            result.storage = std::move(value);
            return true;
        }
        case '[': return ParseArray(result);
        case '{': return ParseObject(result);
        default: return ParseNumber(result);
        }
    }

    template <typename T>
    bool ParseLiteral(std::string_view literal, T value, Value& result)
    {
        if (input_.substr(position_, literal.size()) != literal) {
            return Fail("invalid literal");
        }
        position_ += literal.size();
        result.storage = value;
        return true;
    }

    bool ParseArray(Value& result)
    {
        ++position_;
        Value::Array values;
        SkipWhitespace();
        if (Consume(']')) {
            result.storage = std::move(values);
            return true;
        }
        while (true) {
            Value value;
            if (!ParseValue(value)) return false;
            values.push_back(std::move(value));
            SkipWhitespace();
            if (Consume(']')) break;
            if (!Consume(',')) return Fail("expected ',' or ']'");
            SkipWhitespace();
        }
        result.storage = std::move(values);
        return true;
    }

    bool ParseObject(Value& result)
    {
        ++position_;
        Value::Object values;
        SkipWhitespace();
        if (Consume('}')) {
            result.storage = std::move(values);
            return true;
        }
        while (true) {
            std::string name;
            if (!ParseString(name)) return false;
            SkipWhitespace();
            if (!Consume(':')) return Fail("expected ':'");
            SkipWhitespace();
            Value value;
            if (!ParseValue(value)) return false;
            if (!values.emplace(std::move(name), std::move(value)).second) {
                return Fail("duplicate object property");
            }
            SkipWhitespace();
            if (Consume('}')) break;
            if (!Consume(',')) return Fail("expected ',' or '}'");
            SkipWhitespace();
        }
        result.storage = std::move(values);
        return true;
    }

    bool ParseString(std::string& result)
    {
        if (!Consume('"')) return Fail("expected string");
        result.clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return true;
            if (character < 0x20) return Fail("unescaped control character");
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) return Fail("incomplete escape");
            switch (input_[position_++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                std::uint32_t codepoint = 0;
                if (!ParseHex4(codepoint)) return false;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if (position_ + 2 > input_.size() || input_.substr(position_, 2) != "\\u") {
                        return Fail("missing low surrogate");
                    }
                    position_ += 2;
                    std::uint32_t low = 0;
                    if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                        return Fail("invalid low surrogate");
                    }
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    return Fail("unexpected low surrogate");
                }
                AppendUtf8(codepoint, result);
                break;
            }
            default: return Fail("invalid escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseHex4(std::uint32_t& value)
    {
        if (position_ + 4 > input_.size()) return Fail("incomplete unicode escape");
        value = 0;
        for (int count = 0; count < 4; ++count) {
            const char character = input_[position_++];
            value <<= 4;
            if (character >= '0' && character <= '9') value += character - '0';
            else if (character >= 'a' && character <= 'f') value += character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') value += character - 'A' + 10;
            else return Fail("invalid unicode escape");
        }
        return true;
    }

    static void AppendUtf8(std::uint32_t codepoint, std::string& output)
    {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool ParseNumber(Value& result)
    {
        const std::size_t start = position_;
        if (Consume('-') && position_ >= input_.size()) return Fail("invalid number");
        if (Consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                return Fail("leading zero in number");
            }
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' || input_[position_] > '9') {
                return Fail("invalid number");
            }
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        bool integer = true;
        if (Consume('.')) {
            integer = false;
            if (!ConsumeDigits()) return Fail("invalid fraction");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            integer = false;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (!ConsumeDigits()) return Fail("invalid exponent");
        }
        const std::string_view token = input_.substr(start, position_ - start);
        if (integer) {
            std::int64_t value = 0;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (parsed.ec == std::errc() && parsed.ptr == token.data() + token.size()) {
                result.storage = value;
                return true;
            }
        }
        std::string terminated(token);
        char* end = nullptr;
        const double value = std::strtod(terminated.c_str(), &end);
        if (end != terminated.c_str() + terminated.size() || !std::isfinite(value)) {
            return Fail("number out of range");
        }
        result.storage = value;
        return true;
    }

    bool ConsumeDigits()
    {
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        return position_ != start;
    }

    void SkipWhitespace()
    {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n') break;
            ++position_;
        }
    }

    bool Consume(char expected)
    {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool Fail(const char* message)
    {
        if (error_.empty()) error_ = message;
        return false;
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::size_t depth_ = 0;
    std::string error_;
};

} // namespace

const Value::Object* Value::AsObject() const { return std::get_if<Object>(&storage); }
const Value::Array* Value::AsArray() const { return std::get_if<Array>(&storage); }
const std::string* Value::AsString() const { return std::get_if<std::string>(&storage); }
const std::int64_t* Value::AsInteger() const { return std::get_if<std::int64_t>(&storage); }

const Value* Value::Find(std::string_view name) const
{
    const Object* object = AsObject();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

bool Parse(std::string_view input, Value& result, std::string& error)
{
    if (!IsValidUtf8(input)) {
        error = "invalid UTF-8";
        return false;
    }
    return Parser(input).Run(result, error);
}

std::string Quote(std::string_view value)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                result += "\\u00";
                result.push_back(Hex[character >> 4]);
                result.push_back(Hex[character & 0x0F]);
            } else {
                result.push_back(static_cast<char>(character));
            }
        }
    }
    result.push_back('"');
    return result;
}

} // namespace douyin::json
