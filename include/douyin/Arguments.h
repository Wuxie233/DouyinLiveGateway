#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace douyin {

struct Arguments {
    std::wstring log_path;
};

bool ParseArguments(const std::vector<std::wstring>& values, Arguments& result, std::string& error);

} // namespace douyin
