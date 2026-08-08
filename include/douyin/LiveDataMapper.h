#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace douyin {

inline constexpr std::string_view HeartGiftId =
    "VQCVfwVeQCJZDCr/srE99db/NA3vgkBlaxG2dPyetE8NLgDRQ5ktGcwXm8M=";

struct MappingResult {
    std::vector<std::string> lines;
    std::vector<std::string> diagnostics;
};

MappingResult MapOpenLiveData(std::string_view callback_bytes, std::string_view session_id);
std::string PingLine();
std::string SessionStartLine(std::string_view session_id);
std::string SessionEndLine(std::string_view session_id);

} // namespace douyin
