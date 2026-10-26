#ifndef OSRM_DEPLOYMENT_DESCRIPTOR_HPP
#define OSRM_DEPLOYMENT_DESCRIPTOR_HPP

#include "deployment/config.hpp"

namespace osrm::deployment
{
bool writeCompletionDescriptor(const DeploymentConfig &config, std::string &error);
bool validatePreparedData(const DeploymentConfig &config, std::string &error);
} // namespace osrm::deployment
#endif
