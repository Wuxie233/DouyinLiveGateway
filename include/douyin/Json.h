#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace douyin::json {

struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    Storage storage;

    const Object* AsObject() const;
    const Array* AsArray() const;
    const std::string* AsString() const;
    const std::int64_t* AsInteger() const;
    const Value* Find(std::string_view name) const;
};

bool Parse(std::string_view input, Value& result, std::string& error);
std::string Quote(std::string_view value);

} // namespace douyin::json
