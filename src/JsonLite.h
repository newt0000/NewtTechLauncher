#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

class JsonValue
{
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

    JsonValue();
    explicit JsonValue(Storage value);

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isObject() const;
    bool isArray() const;

    bool asBool(bool defaultValue = false) const;
    double asNumber(double defaultValue = 0.0) const;
    std::string asString(const std::string& defaultValue = "") const;

    const Object& asObject() const;
    const Array& asArray() const;
    const JsonValue& get(const std::string& key) const;

private:
    Storage value_;
};

class JsonLite
{
public:
    static JsonValue parse(const std::string& text);
};
