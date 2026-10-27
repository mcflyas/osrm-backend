#ifndef OSRM_SERVER_MULTI_PROFILE_SERVICE_HANDLER_HPP
#define OSRM_SERVER_MULTI_PROFILE_SERVICE_HANDLER_HPP

#include "server/service_handler.hpp"

namespace osrm::server
{
class MultiProfileServiceHandler final : public ServiceHandlerInterface
{
  public:
    explicit MultiProfileServiceHandler(
        std::unordered_map<std::string, std::unique_ptr<ServiceHandlerInterface>> handlers);
    engine::Status RunQuery(api::ParsedURL url, engine::api::ResultT &result) override;
    engine::Status
    RunQuery(api::ParsedURL url, const std::string &body, engine::api::ResultT &result) override;

  private:
    ServiceHandlerInterface *resolveProfile(const std::string &name, engine::api::ResultT &result);
    const std::unordered_map<std::string, std::unique_ptr<ServiceHandlerInterface>> m_handlers;
};
} // namespace osrm::server
#endif
