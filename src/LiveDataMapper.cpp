#include "douyin/LiveDataMapper.h"

#include "douyin/Json.h"

#include <limits>

namespace douyin {
namespace {

constexpr std::size_t MaxIdentifierBytes = 256;

std::size_t Utf16CodeUnits(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7F) {
            ++index;
            ++count;
        } else if (first <= 0xDF) {
            index += 2;
            ++count;
        } else if (first <= 0xEF) {
            index += 3;
            ++count;
        } else {
            index += 4;
            count += 2;
        }
    }
    return count;
}

const std::string* RequiredString(const json::Value& object, std::string_view name)
{
    const json::Value* value = object.Find(name);
    const std::string* text = value ? value->AsString() : nullptr;
    return text && !text->empty() && Utf16CodeUnits(*text) <= MaxIdentifierBytes ? text : nullptr;
}

bool ResolveType(const json::Value& item, std::string& type)
{
    const json::Value* numeric_value = item.Find("msg_type");
    const json::Value* string_value = item.Find("msg_type_str");
    const std::int64_t* numeric = numeric_value ? numeric_value->AsInteger() : nullptr;
    const std::string* named = string_value ? string_value->AsString() : nullptr;
    if (!numeric && !named) return false;

    std::string from_number;
    if (numeric) {
        switch (*numeric) {
        case 1: from_number = "live_like"; break;
        case 2: from_number = "live_comment"; break;
        case 3: from_number = "live_gift"; break;
        case 4: from_number = "live_fansclub"; break;
        case 5: from_number = "live_follow"; break;
        case 7: from_number = "live_enter_room"; break;
        default: return false;
        }
    }
    if (named) {
        if (*named != "live_like" && *named != "live_comment" && *named != "live_gift"
            && *named != "live_fansclub" && *named != "live_follow"
            && *named != "live_enter_room") {
            return false;
        }
        if (numeric && *named != from_number) return false;
        type = *named;
    } else {
        type = std::move(from_number);
    }
    return true;
}

std::string CommonPrefix(
    std::string_view type,
    std::string_view session_id,
    std::string_view message_id,
    std::string_view viewer_id,
    std::string_view nickname,
    const json::Value* timestamp)
{
    std::string result = "{\"v\":1,\"type\":" + json::Quote(type)
        + ",\"session_id\":" + json::Quote(session_id)
        + ",\"event_id\":" + json::Quote(message_id)
        + ",\"msg_id\":" + json::Quote(message_id)
        + ",\"viewer_id\":" + json::Quote(viewer_id)
        + ",\"display_name\":" + json::Quote(nickname);
    const std::int64_t* timestamp_ms = timestamp ? timestamp->AsInteger() : nullptr;
    if (timestamp_ms && *timestamp_ms >= 0) {
        result += ",\"timestamp_ms\":" + std::to_string(*timestamp_ms);
    }
    return result;
}

bool FollowAction(const json::Value& item, std::int64_t& action)
{
    const json::Value* canonical_value = item.Find("user_follow_action");
    const json::Value* alias_value = item.Find("use_follow_action");
    const std::int64_t* canonical = canonical_value ? canonical_value->AsInteger() : nullptr;
    const std::int64_t* alias = alias_value ? alias_value->AsInteger() : nullptr;
    if (!canonical && !alias) return false;
    if (canonical && alias && *canonical != *alias) return false;
    action = canonical ? *canonical : *alias;
    return true;
}

} // namespace

MappingResult MapOpenLiveData(std::string_view callback_bytes, std::string_view session_id)
{
    MappingResult result;
    if (session_id.empty() || Utf16CodeUnits(session_id) > MaxIdentifierBytes) {
        result.diagnostics.emplace_back("invalid adapter session id");
        return result;
    }

    json::Value root;
    std::string error;
    if (!json::Parse(callback_bytes, root, error)) {
        result.diagnostics.push_back("invalid callback JSON: " + error);
        return result;
    }
    const std::string* envelope_type = RequiredString(root, "type");
    const std::string* event_name = RequiredString(root, "eventName");
    const json::Value* params = root.Find("params");
    const json::Value* payload_value = params ? params->Find("payload") : nullptr;
    const json::Value::Array* payload = payload_value ? payload_value->AsArray() : nullptr;
    if (!envelope_type || *envelope_type != "event" || !event_name
        || *event_name != "OPEN_LIVE_DATA" || !payload) {
        result.diagnostics.emplace_back("ignored non-OPEN_LIVE_DATA envelope");
        return result;
    }

    for (std::size_t index = 0; index < payload->size(); ++index) {
        const json::Value& item = (*payload)[index];
        if (!item.AsObject()) {
            result.diagnostics.push_back("payload[" + std::to_string(index) + "]: expected object");
            continue;
        }
        std::string type;
        const std::string* message_id = RequiredString(item, "msg_id");
        const std::string* viewer_id = RequiredString(item, "sec_open_id");
        const std::string* nickname = RequiredString(item, "nickname");
        const json::Value* timestamp = item.Find("timestamp");
        if (!ResolveType(item, type) || !message_id || !viewer_id || !nickname) {
            result.diagnostics.push_back("payload[" + std::to_string(index) + "]: invalid identity or discriminator");
            continue;
        }

        if (type == "live_comment" || type == "live_like" || type == "live_enter_room") {
            const std::string event_type = type == "live_comment"
                ? "comment" : type == "live_like" ? "like" : "enter_room";
            result.lines.push_back(CommonPrefix(
                event_type, session_id, *message_id, *viewer_id, *nickname, timestamp) + "}");
            continue;
        }
        if (type == "live_follow") {
            std::int64_t action = 0;
            if (!FollowAction(item, action)) {
                result.diagnostics.push_back("payload[" + std::to_string(index) + "]: invalid follow action");
            } else if (action == 1) {
                result.lines.push_back(CommonPrefix(
                    "follow", session_id, *message_id, *viewer_id, *nickname, timestamp) + "}");
            }
            continue;
        }
        if (type == "live_gift") {
            const std::string* gift_id = RequiredString(item, "sec_gift_id");
            const json::Value* gift_count_value = item.Find("gift_num");
            const std::int64_t* gift_count = gift_count_value ? gift_count_value->AsInteger() : nullptr;
            if (!gift_id || *gift_id != HeartGiftId) {
                result.diagnostics.push_back("payload[" + std::to_string(index) + "]: unsupported gift");
            } else if (!gift_count || *gift_count <= 0 || *gift_count > std::numeric_limits<std::int32_t>::max()) {
                result.diagnostics.push_back("payload[" + std::to_string(index) + "]: invalid gift count");
            } else {
                result.lines.push_back(CommonPrefix("gift", session_id, *message_id, *viewer_id, *nickname, timestamp)
                    + ",\"gift_id\":" + json::Quote(*gift_id)
                    + ",\"count\":" + std::to_string(*gift_count) + "}");
            }
            continue;
        }
        result.diagnostics.push_back("payload[" + std::to_string(index) + "]: unsupported event type");
    }
    return result;
}

std::string SessionStartLine(std::string_view session_id)
{
    return "{\"v\":1,\"type\":\"session_start\",\"session_id\":" + json::Quote(session_id) + "}";
}

std::string PingLine()
{
    return "{\"v\":1,\"type\":\"ping\"}";
}

std::string SessionEndLine(std::string_view session_id)
{
    return "{\"v\":1,\"type\":\"session_end\",\"session_id\":" + json::Quote(session_id) + "}";
}

} // namespace douyin
