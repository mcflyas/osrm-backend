#ifndef OSRM_DEPLOYMENT_STORE_HPP
#define OSRM_DEPLOYMENT_STORE_HPP

#include "deployment/config.hpp"

namespace osrm::deployment
{
int publishProfiles(const DeploymentConfig &config, int max_wait, std::string &error);
}
#endif
