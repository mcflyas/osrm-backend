#include "server/multi_profile_service_handler.hpp"

#include "server/api/parsed_url.hpp"
#include "util/json_container.hpp"

#include <boost/assert.hpp>

#include <algorithm>

namespace osrm::server
{
MultiProfileServiceHandler::MultiProfileServiceHandler(
    std::unordered_map<std::string, std::unique_ptr<ServiceHandlerInterface>> handlers)
    : m_handlers(std::move(handlers))
{
    BOOST_ASSERT(std::all_of(m_handlers.begin(),
                             m_handlers.end(),
                             [](const auto &entry) { return bool(entry.second); }));
}

ServiceHandlerInterface *MultiProfileServiceHandler::resolveProfile(const std::string &name,
                                                                    engine::api::ResultT &result)
{
    const auto found = m_handlers.find(name);
    if (found != m_handlers.end())
        return found->second.get();
    result = util::json::Object{};
    auto &object = std::get<util::json::Object>(result);
    object.values["code"] = "InvalidProfile";
    object.values["message"] = "Profile " + name + " not found!";
    return nullptr;
}

engine::Status MultiProfileServiceHandler::RunQuery(api::ParsedURL url,
                                                    engine::api::ResultT &result)
{
    auto *handler = resolveProfile(url.profile, result);
    return handler ? handler->RunQuery(std::move(url), result) : engine::Status::Error;
}

engine::Status MultiProfileServiceHandler::RunQuery(api::ParsedURL url,
                                                    const std::string &body,
                                                    engine::api::ResultT &result)
{
    auto *handler = resolveProfile(url.profile, result);
    return handler ? handler->RunQuery(std::move(url), body, result) : engine::Status::Error;
}
} // namespace osrm::server
