#ifndef OSRM_DEPLOYMENT_CONFIG_HPP
#define OSRM_DEPLOYMENT_CONFIG_HPP

#include "engine/engine_config.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osrm::deployment
{
enum class StorageMode
{
    MMap,
    Memory,
    SharedMemory
};
enum class ConfigPurpose
{
    Prepare,
    Serve,
    Store
};

struct ProfileConfig
{
    std::string Name;
    std::filesystem::path Lua;
    engine::EngineConfig::Algorithm Algorithm;
};

struct ServerOptions
{
    std::string Ip = "0.0.0.0";
    int Port = 5000;
    int Threads = 1;
    short KeepaliveTimeout = 5;
    unsigned MaxHeaderSize = 0;
    std::uint64_t MaxBodySize = 0;
    StorageMode Storage = StorageMode::MMap;
    std::string DatasetNamespace;
    engine::EngineConfig EngineDefaults;
};

struct DeploymentConfig
{
    std::filesystem::path Input;
    std::filesystem::path OutputDirectory;
    unsigned PreparationThreads = 1;
    ServerOptions Server;
    std::vector<ProfileConfig> Profiles;
};

ServerOptions defaultServerOptions();
std::optional<DeploymentConfig> readConfig(const std::filesystem::path &path, std::string &error);
bool validateConfig(const DeploymentConfig &config, ConfigPurpose purpose, std::string &error);
std::filesystem::path datasetBase(const DeploymentConfig &config, const ProfileConfig &profile);
std::string datasetName(const DeploymentConfig &config, const ProfileConfig &profile);
engine::EngineConfig makeEngineConfig(const DeploymentConfig &config, const ProfileConfig &profile);
} // namespace osrm::deployment
#endif
