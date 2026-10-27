#ifndef OSRM_DEPLOYMENT_JSON_HPP
#define OSRM_DEPLOYMENT_JSON_HPP

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace osrm::deployment::detail
{
inline bool fail(std::string &error, std::string message)
{
    error = std::move(message);
    return false;
}

inline bool uniqueKeys(const rapidjson::Value &value, const std::string &field, std::string &error)
{
    if (value.IsObject())
    {
        std::unordered_set<std::string_view> keys;
        for (const auto &member : value.GetObject())
        {
            const std::string key(member.name.GetString(), member.name.GetStringLength());
            if (!keys.insert({member.name.GetString(), member.name.GetStringLength()}).second)
                return fail(error, field + "." + key + ": duplicate field");
            if (!uniqueKeys(member.value, field + "." + key, error))
                return false;
        }
    }
    else if (value.IsArray())
    {
        for (rapidjson::SizeType i = 0; i < value.Size(); ++i)
            if (!uniqueKeys(value[i], field + "[" + std::to_string(i) + "]", error))
                return false;
    }
    return true;
}

inline std::optional<rapidjson::Document> readJson(const std::filesystem::path &path,
                                                   std::string &error)
{
    error.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        fail(error, "Cannot read " + path.string());
        return std::nullopt;
    }
    rapidjson::IStreamWrapper wrapper(stream);
    rapidjson::Document document;
    document.ParseStream<rapidjson::kParseValidateEncodingFlag>(wrapper);
    if (document.HasParseError())
    {
        fail(error,
             path.string() + ": " + rapidjson::GetParseError_En(document.GetParseError()) +
                 " at byte " + std::to_string(document.GetErrorOffset()));
        return std::nullopt;
    }
    if (stream.bad() || !uniqueKeys(document, "$", error))
    {
        if (error.empty())
            fail(error, "Cannot read " + path.string());
        return std::nullopt;
    }
    return document;
}

inline bool members(const rapidjson::Value &value,
                    std::initializer_list<std::string_view> allowed,
                    const std::string &field,
                    std::string &error)
{
    if (!value.IsObject())
        return fail(error, field + ": expected an object");
    for (const auto &member : value.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return fail(error, field + "." + std::string(key) + ": unknown field");
    }
    return true;
}

inline bool string(const rapidjson::Value &value,
                   std::string &result,
                   const std::string &field,
                   std::string &error)
{
    if (!value.IsString() || value.GetStringLength() == 0)
        return fail(error, field + ": expected a nonempty string");
    result.assign(value.GetString(), value.GetStringLength());
    if (result.find('\0') != std::string::npos)
        return fail(error, field + ": embedded NUL is not allowed");
    return true;
}

template <typename T>
bool integer(const rapidjson::Value &value, T &result, const std::string &field, std::string &error)
{
    if constexpr (std::is_signed_v<T>)
    {
        if (!value.IsInt64() || value.GetInt64() < std::numeric_limits<T>::min() ||
            value.GetInt64() > std::numeric_limits<T>::max())
            return fail(error, field + ": integer out of range");
        result = static_cast<T>(value.GetInt64());
    }
    else
    {
        if (!value.IsUint64() || value.GetUint64() > std::numeric_limits<T>::max())
            return fail(error, field + ": unsigned integer out of range");
        result = static_cast<T>(value.GetUint64());
    }
    return true;
}
} // namespace osrm::deployment::detail
#endif
