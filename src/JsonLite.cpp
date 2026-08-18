#include "JsonLite.h"

#include <cctype>
#include <stdexcept>

namespace
{
void appendUtf8(std::string& out, unsigned code)
{
    if (code <= 0x7F)
        out.push_back(static_cast<char>(code));
    else if (code <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class Parser
{
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue parse()
    {
        skip();
        JsonValue result = parseValue();
        skip();

        if (pos_ != text_.size())
            throw std::runtime_error("Unexpected trailing JSON data.");

        return result;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    void skip()
    {
        while (
            pos_ < text_.size() &&
            std::isspace(static_cast<unsigned char>(text_[pos_]))
        )
            ++pos_;
    }

    char peek() const
    {
        if (pos_ >= text_.size())
            throw std::runtime_error("Unexpected end of JSON.");

        return text_[pos_];
    }

    char take()
    {
        char c = peek();
        ++pos_;
        return c;
    }

    void expect(char c)
    {
        if (take() != c)
            throw std::runtime_error("Unexpected JSON token.");
    }

    JsonValue parseValue()
    {
        skip();

        const char c = peek();

        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue(parseString());
        if (c == 't') return literal("true", JsonValue(true));
        if (c == 'f') return literal("false", JsonValue(false));
        if (c == 'n') return literal("null", JsonValue());

        if (
            c == '-' ||
            std::isdigit(static_cast<unsigned char>(c))
        )
            return JsonValue(parseNumber());

        throw std::runtime_error("Invalid JSON value.");
    }

    JsonValue parseObject()
    {
        JsonValue::Object object;

        expect('{');
        skip();

        if (peek() == '}')
        {
            take();
            return JsonValue(object);
        }

        for (;;)
        {
            skip();
            std::string key = parseString();

            skip();
            expect(':');
            skip();

            object.emplace(key, parseValue());

            skip();

            const char c = take();

            if (c == '}')
                break;

            if (c != ',')
                throw std::runtime_error("Expected ',' or '}'.");
        }

        return JsonValue(object);
    }

    JsonValue parseArray()
    {
        JsonValue::Array array;

        expect('[');
        skip();

        if (peek() == ']')
        {
            take();
            return JsonValue(array);
        }

        for (;;)
        {
            array.push_back(parseValue());

            skip();

            const char c = take();

            if (c == ']')
                break;

            if (c != ',')
                throw std::runtime_error("Expected ',' or ']'.");

            skip();
        }

        return JsonValue(array);
    }

    std::string parseString()
    {
        expect('"');

        std::string result;

        while (pos_ < text_.size())
        {
            char c = take();

            if (c == '"')
                return result;

            if (c != '\\')
            {
                result.push_back(c);
                continue;
            }

            char e = take();

            switch (e)
            {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;

                case 'u':
                {
                    unsigned code = 0;

                    for (int i = 0; i < 4; ++i)
                    {
                        char h = take();
                        code <<= 4;

                        if (h >= '0' && h <= '9') code |= h - '0';
                        else if (h >= 'a' && h <= 'f') code |= 10 + h - 'a';
                        else if (h >= 'A' && h <= 'F') code |= 10 + h - 'A';
                        else throw std::runtime_error("Invalid unicode escape.");
                    }

                    appendUtf8(result, code);
                    break;
                }

                default:
                    throw std::runtime_error("Unsupported JSON escape.");
            }
        }

        throw std::runtime_error("Unterminated JSON string.");
    }

    double parseNumber()
    {
        const size_t start = pos_;

        if (peek() == '-')
            ++pos_;

        while (
            pos_ < text_.size() &&
            std::isdigit(static_cast<unsigned char>(text_[pos_]))
        )
            ++pos_;

        if (
            pos_ < text_.size() &&
            text_[pos_] == '.'
        )
        {
            ++pos_;

            while (
                pos_ < text_.size() &&
                std::isdigit(static_cast<unsigned char>(text_[pos_]))
            )
                ++pos_;
        }

        return std::stod(
            text_.substr(
                start,
                pos_ - start
            )
        );
    }

    JsonValue literal(
        const std::string& word,
        const JsonValue& value)
    {
        if (text_.substr(pos_, word.size()) != word)
            throw std::runtime_error("Invalid JSON literal.");

        pos_ += word.size();
        return value;
    }
};
}

JsonValue::JsonValue() : value_(nullptr) {}
JsonValue::JsonValue(Storage value) : value_(std::move(value)) {}

bool JsonValue::isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
bool JsonValue::isBool() const { return std::holds_alternative<bool>(value_); }
bool JsonValue::isNumber() const { return std::holds_alternative<double>(value_); }
bool JsonValue::isString() const { return std::holds_alternative<std::string>(value_); }
bool JsonValue::isObject() const { return std::holds_alternative<Object>(value_); }
bool JsonValue::isArray() const { return std::holds_alternative<Array>(value_); }

bool JsonValue::asBool(bool d) const
{
    return isBool() ? std::get<bool>(value_) : d;
}

double JsonValue::asNumber(double d) const
{
    return isNumber() ? std::get<double>(value_) : d;
}

std::string JsonValue::asString(const std::string& d) const
{
    return isString() ? std::get<std::string>(value_) : d;
}

const JsonValue::Object& JsonValue::asObject() const
{
    if (!isObject())
        throw std::runtime_error("JSON value is not an object.");

    return std::get<Object>(value_);
}

const JsonValue::Array& JsonValue::asArray() const
{
    if (!isArray())
        throw std::runtime_error("JSON value is not an array.");

    return std::get<Array>(value_);
}

const JsonValue& JsonValue::get(const std::string& key) const
{
    static JsonValue nullValue;

    if (!isObject())
        return nullValue;

    const auto& object = std::get<Object>(value_);
    auto it = object.find(key);

    return it == object.end()
        ? nullValue
        : it->second;
}

JsonValue JsonLite::parse(const std::string& text)
{
    return Parser(text).parse();
}
