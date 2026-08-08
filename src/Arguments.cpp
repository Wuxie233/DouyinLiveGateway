#include "douyin/Arguments.h"

namespace douyin {
namespace {

bool ReadOption(
    const std::vector<std::wstring>& values,
    std::size_t& index,
    const std::wstring& name,
    std::wstring& value)
{
    const std::wstring prefix = name + L"=";
    const std::wstring& argument = values[index];
    if (argument.rfind(prefix, 0) == 0) {
        value = argument.substr(prefix.size());
        return true;
    }
    if (argument == name) {
        if (index + 1 >= values.size() || values[index + 1].rfind(L"--", 0) == 0) {
            value.clear();
            return true;
        }
        value = values[++index];
        return true;
    }
    return false;
}

} // namespace

bool ParseArguments(const std::vector<std::wstring>& values, Arguments& result, std::string& error)
{
    result = {};
    error.clear();
    for (std::size_t index = 0; index < values.size(); ++index) {
        std::wstring value;
        if (ReadOption(values, index, L"--logPath", value)) {
            if (value.empty()) {
                error = "--logPath requires a value";
                return false;
            }
            result.log_path = std::move(value);
            continue;
        }
        // Ignore unrelated command-line arguments when invoked from a local wrapper.
    }
    return true;
}

} // namespace douyin
