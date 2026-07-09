#pragma once

// Single include point for the vendored nlohmann/json (MIT).
// Exceptions are disabled repo-wide for parsing: use json::parse(..., nullptr, false)
// and check is_discarded(). The typed accessors below NEVER throw — nlohmann's own
// value()/operator[] still throw type_error on type-confused input, and this data
// comes off the network, so protocol code must go through these helpers instead.
#include <nlohmann/json.hpp>

#include <string>

namespace rl {
using Json = nlohmann::json;

// Non-throwing typed field access. All return a sensible empty/default when the
// receiver is not an object, the key is missing, or the stored type mismatches.
inline const Json &jsonRef(const Json &j, const char *key)
{
    static const Json null;
    if (!j.is_object())
        return null;
    const auto it = j.find(key);
    return it != j.end() ? *it : null;
}

inline Json jsonObj(const Json &j, const char *key)
{
    const Json &v = jsonRef(j, key);
    return v.is_object() ? v : Json::object();
}

inline Json jsonArr(const Json &j, const char *key)
{
    const Json &v = jsonRef(j, key);
    return v.is_array() ? v : Json::array();
}

inline std::string jsonStr(const Json &j, const char *key, const char *dflt = "")
{
    const Json &v = jsonRef(j, key);
    return v.is_string() ? v.get<std::string>() : std::string(dflt);
}

inline int jsonInt(const Json &j, const char *key, int dflt = 0)
{
    const Json &v = jsonRef(j, key);
    if (v.is_number_integer() || v.is_number_unsigned())
        return v.get<int>();
    // Some firmware serializes numbers as strings ("channelNum":"8").
    if (v.is_string()) {
        try {
            return std::stoi(v.get<std::string>());
        } catch (...) {
            return dflt;
        }
    }
    if (v.is_boolean())
        return v.get<bool>() ? 1 : 0;
    return dflt;
}

inline bool jsonBool(const Json &j, const char *key, bool dflt = false)
{
    const Json &v = jsonRef(j, key);
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number_integer() || v.is_number_unsigned())
        return v.get<int>() != 0;
    return dflt;
}

} // namespace rl
