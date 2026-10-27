#ifndef OSRM_DEPLOYMENT_PREPARE_HPP
#define OSRM_DEPLOYMENT_PREPARE_HPP

#include "deployment/config.hpp"

namespace osrm::deployment
{
int prepare(const DeploymentConfig &config, std::string &error);
}
#endif
