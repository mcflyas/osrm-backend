#include "deployment/config.hpp"

#include "json.hpp"
#include "storage/shared_datatype.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <thread>
#include <unordered_set>

namespace osrm::deployment
{
namespace
{
using Algorithm = engine::EngineConfig::Algorithm;

bool readPath(const rapidjson::Value &value,
              std::filesystem::path &result,
              const std::filesystem::path &base,
              const std::string &field,
              std::string &error)
{
    std::string text;
    if (!detail::string(value, text, field, error))
        return false;
    result = (base / std::filesystem::path(text)).lexically_normal();
    return true;
}

bool validName(const std::string &name)
{
    return !name.empty() && std::all_of(name.begin(),
                                        name.end(),
                                        [](unsigned char c)
                                        {
                                            return (c >= 'a' && c <= 'z') ||
                                                   (c >= 'A' && c <= 'Z') ||
                                                   (c >= '0' && c <= '9') || c == '_' || c == '-';
                                        });
}

bool readRadius(const rapidjson::Value &value,
                double &result,
                const std::string &field,
                std::string &error)
{
    if (value.IsString() &&
        std::string_view(value.GetString(), value.GetStringLength()) == "unlimited")
        result = -1;
    else if (value.IsNumber())
        result = value.GetDouble();
    else
        return detail::fail(error, field + ": expected a number or unlimited");
    return true;
}

bool readServer(const rapidjson::Value &value, ServerOptions &server, std::string &error)
{
    if (!detail::members(value,
                         {"ip",
                          "port",
                          "threads",
                          "keepalive_timeout",
                          "max_header_size",
                          "max_request_body_size",
                          "storage",
                          "dataset_namespace",
                          "verbosity",
                          "max_viaroute_size",
                          "max_trip_size",
                          "max_table_size",
                          "max_matching_size",
                          "max_nearest_size",
                          "max_nearest_locations",
                          "max_alternatives",
                          "max_matching_radius",
                          "default_radius",
                          "disable_feature_dataset"},
                         "server",
                         error))
        return false;
    const auto text = [&](const char *key, std::string &target)
    {
        return !value.HasMember(key) ||
               detail::string(value[key], target, std::string("server.") + key, error);
    };
    const auto number = [&](const char *key, auto &target)
    {
        return !value.HasMember(key) ||
               detail::integer(value[key], target, std::string("server.") + key, error);
    };
    if (!text("ip", server.Ip) || !text("dataset_namespace", server.DatasetNamespace) ||
        !text("verbosity", server.EngineDefaults.verbosity) || !number("port", server.Port) ||
        !number("threads", server.Threads) ||
        !number("keepalive_timeout", server.KeepaliveTimeout) ||
        !number("max_header_size", server.MaxHeaderSize) ||
        !number("max_request_body_size", server.MaxBodySize))
        return false;
    const std::array<std::pair<const char *, int engine::EngineConfig::*>, 7> limits{
        {{"max_viaroute_size", &engine::EngineConfig::max_locations_viaroute},
         {"max_trip_size", &engine::EngineConfig::max_locations_trip},
         {"max_table_size", &engine::EngineConfig::max_locations_distance_table},
         {"max_matching_size", &engine::EngineConfig::max_locations_map_matching},
         {"max_nearest_size", &engine::EngineConfig::max_results_nearest},
         {"max_nearest_locations", &engine::EngineConfig::max_locations_nearest},
         {"max_alternatives", &engine::EngineConfig::max_alternatives}}};
    for (const auto &[key, member] : limits)
        if (!number(key, server.EngineDefaults.*member))
            return false;
    for (const auto &[key, member] :
         {std::pair{"max_matching_radius", &engine::EngineConfig::max_radius_map_matching},
          std::pair{"default_radius", &engine::EngineConfig::default_radius}})
        if (value.HasMember(key) &&
            !readRadius(
                value[key], server.EngineDefaults.*member, std::string("server.") + key, error))
            return false;
    if (value.HasMember("storage"))
    {
        std::string storage;
        if (!text("storage", storage))
            return false;
        if (storage == "mmap")
            server.Storage = StorageMode::MMap;
        else if (storage == "memory")
            server.Storage = StorageMode::Memory;
        else if (storage == "shared_memory")
            server.Storage = StorageMode::SharedMemory;
        else
            return detail::fail(error, "server.storage: expected mmap, memory, or shared_memory");
    }
    if (value.HasMember("disable_feature_dataset"))
    {
        const auto &features = value["disable_feature_dataset"];
        if (!features.IsArray())
            return detail::fail(error, "server.disable_feature_dataset: expected an array");
        for (const auto &feature : features.GetArray())
        {
            std::string name;
            if (!detail::string(feature, name, "server.disable_feature_dataset", error))
                return false;
            if (name == "ROUTE_STEPS" || name == "route_steps")
                server.EngineDefaults.disable_feature_dataset.push_back(
                    storage::FeatureDataset::ROUTE_STEPS);
            else if (name == "ROUTE_GEOMETRY" || name == "route_geometry")
                server.EngineDefaults.disable_feature_dataset.push_back(
                    storage::FeatureDataset::ROUTE_GEOMETRY);
            else
                return detail::fail(error,
                                    "server.disable_feature_dataset: unknown feature " + name);
        }
    }
    return true;
}
} // namespace

ServerOptions defaultServerOptions()
{
    ServerOptions result;
    result.Threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    auto &engine = result.EngineDefaults;
    engine.max_locations_viaroute = 500;
    engine.max_locations_trip = 100;
    engine.max_locations_distance_table = 100;
    engine.max_locations_map_matching = 100;
    engine.max_results_nearest = 100;
#ifdef NDEBUG
    engine.verbosity = "INFO";
#else
    engine.verbosity = "DEBUG";
#endif
    return result;
}

std::optional<DeploymentConfig> readConfig(const std::filesystem::path &path, std::string &error)
{
    auto json = detail::readJson(path, error);
    if (!json || !detail::members(
                     *json,
                     {"version", "input", "output_directory", "preparation", "server", "profiles"},
                     "$",
                     error))
        return std::nullopt;
    const auto &value = *json;
    if (!value.HasMember("version") || !value["version"].IsInt() ||
        value["version"].GetInt() != 1 || !value.HasMember("output_directory") ||
        !value.HasMember("profiles"))
    {
        detail::fail(error, "version must be 1; output_directory and profiles are required");
        return std::nullopt;
    }
    std::error_code code;
    const auto base = std::filesystem::absolute(path, code).parent_path();
    if (code)
    {
        detail::fail(error, path.string() + ": " + code.message());
        return std::nullopt;
    }
    DeploymentConfig result;
    result.Server = defaultServerOptions();
    result.PreparationThreads = static_cast<unsigned>(result.Server.Threads);
    if (!readPath(
            value["output_directory"], result.OutputDirectory, base, "output_directory", error) ||
        (value.HasMember("input") && !readPath(value["input"], result.Input, base, "input", error)))
        return std::nullopt;
    if (value.HasMember("server") && !readServer(value["server"], result.Server, error))
        return std::nullopt;
    if (value.HasMember("preparation"))
    {
        const auto &preparation = value["preparation"];
        if (!detail::members(preparation, {"threads"}, "preparation", error) ||
            (preparation.HasMember("threads") &&
             !detail::integer(
                 preparation["threads"], result.PreparationThreads, "preparation.threads", error)))
            return std::nullopt;
    }
    const auto &profiles = value["profiles"];
    if (!profiles.IsArray())
    {
        detail::fail(error, "profiles: expected an array");
        return std::nullopt;
    }
    for (rapidjson::SizeType i = 0; i < profiles.Size(); ++i)
    {
        const auto field = "profiles[" + std::to_string(i) + "]";
        const auto &profile = profiles[i];
        if (!detail::members(profile, {"name", "lua", "algorithm"}, field, error))
            return std::nullopt;
        if (!profile.HasMember("name") || !profile.HasMember("algorithm"))
        {
            detail::fail(error, field + ": name and algorithm are required");
            return std::nullopt;
        }
        ProfileConfig entry;
        std::string algorithm;
        if (!detail::string(profile["name"], entry.Name, field + ".name", error) ||
            !detail::string(profile["algorithm"], algorithm, field + ".algorithm", error))
            return std::nullopt;
        if (algorithm == "CH")
            entry.Algorithm = Algorithm::CH;
        else if (algorithm == "MLD")
            entry.Algorithm = Algorithm::MLD;
        else
        {
            detail::fail(error, field + ".algorithm: expected CH or MLD");
            return std::nullopt;
        }
        if (profile.HasMember("lua") &&
            !readPath(profile["lua"], entry.Lua, base, field + ".lua", error))
            return std::nullopt;
        result.Profiles.push_back(std::move(entry));
    }
    if (!validateConfig(result, ConfigPurpose::Serve, error))
        return std::nullopt;
    return result;
}

bool validateConfig(const DeploymentConfig &config, ConfigPurpose purpose, std::string &error)
{
    error.clear();
    if (config.OutputDirectory.empty())
        return detail::fail(error, "output_directory is required");
    if (config.PreparationThreads == 0)
        return detail::fail(error, "preparation.threads must be positive");
    const auto &server = config.Server;
    if (server.Port < 0 || server.Port > 65535)
        return detail::fail(error, "server.port must be between 0 and 65535");
    if (server.Threads <= 0)
        return detail::fail(error, "server.threads must be positive");
    if (server.KeepaliveTimeout <= 0)
        return detail::fail(error, "server.keepalive_timeout must be positive");
    const auto &engine = server.EngineDefaults;
    for (const auto &[name, limit] :
         {std::pair{"max_viaroute_size", engine.max_locations_viaroute},
          std::pair{"max_trip_size", engine.max_locations_trip},
          std::pair{"max_table_size", engine.max_locations_distance_table},
          std::pair{"max_matching_size", engine.max_locations_map_matching}})
        if (limit != -1 && limit <= 2)
            return detail::fail(error,
                                std::string("server.") + name + " must be -1 or greater than 2");
    if ((engine.max_results_nearest != -1 && engine.max_results_nearest <= 0) ||
        (engine.max_locations_nearest != -1 && engine.max_locations_nearest <= 0))
        return detail::fail(
            error,
            "server.max_nearest_size and server.max_nearest_locations must be -1 or positive");
    if (engine.max_alternatives < 0)
        return detail::fail(error, "server.max_alternatives must be nonnegative");
    for (const auto &[name, radius] :
         {std::pair{"default_radius", engine.default_radius},
          std::pair{"max_matching_radius", engine.max_radius_map_matching}})
        if (!std::isfinite(radius) || (radius != -1 && radius <= 0))
            return detail::fail(error, std::string("server.") + name + " must be -1 or positive");
    std::string verbosity = engine.verbosity;
    std::transform(verbosity.begin(),
                   verbosity.end(),
                   verbosity.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (verbosity != "NONE" && verbosity != "ERROR" && verbosity != "WARNING" &&
        verbosity != "INFO" && verbosity != "DEBUG")
        return detail::fail(error, "server.verbosity: invalid log level");
    if (server.Storage == StorageMode::SharedMemory && server.DatasetNamespace.empty())
        return detail::fail(error, "server.dataset_namespace is required for shared memory");
    if (purpose == ConfigPurpose::Store && server.Storage != StorageMode::SharedMemory)
        return detail::fail(error, "server.storage must be shared_memory for datastore");
    if (config.Profiles.empty())
        return detail::fail(error, "profiles must not be empty");
    std::unordered_set<std::string> names;
    for (const auto &profile : config.Profiles)
    {
        if (!validName(profile.Name) || !names.insert(profile.Name).second)
            return detail::fail(error, "profiles.name: invalid or duplicate name " + profile.Name);
        if (profile.Algorithm != Algorithm::CH && profile.Algorithm != Algorithm::MLD)
            return detail::fail(error, "profiles.algorithm: invalid algorithm for " + profile.Name);
        if (server.Storage == StorageMode::SharedMemory &&
            datasetName(config, profile).size() + std::string_view("/updatable").size() >
                storage::SharedRegion::MAX_NAME_LENGTH)
            return detail::fail(
                error,
                "server.dataset_namespace: derived shared-memory name too long for " +
                    profile.Name);
        if (purpose == ConfigPurpose::Prepare)
        {
            std::error_code code;
            if (!std::filesystem::is_regular_file(profile.Lua, code))
                return detail::fail(error,
                                    "profiles." + profile.Name + ".lua: cannot read " +
                                        profile.Lua.string());
        }
    }
    if (purpose == ConfigPurpose::Prepare)
    {
        std::error_code code;
        if (!std::filesystem::is_regular_file(config.Input, code))
            return detail::fail(error, "input: cannot read " + config.Input.string());
    }
    return true;
}

std::filesystem::path datasetBase(const DeploymentConfig &config, const ProfileConfig &profile)
{ return config.OutputDirectory / profile.Name / "map.osrm"; }

std::string datasetName(const DeploymentConfig &config, const ProfileConfig &profile)
{ return config.Server.DatasetNamespace + "/" + profile.Name; }

engine::EngineConfig makeEngineConfig(const DeploymentConfig &config, const ProfileConfig &profile)
{
    auto result = config.Server.EngineDefaults;
    result.algorithm = profile.Algorithm;
    result.use_shared_memory = config.Server.Storage == StorageMode::SharedMemory;
    result.use_mmap = config.Server.Storage == StorageMode::MMap;
    result.memory_file.clear();
    result.storage_config =
        result.use_shared_memory
            ? storage::StorageConfig{}
            : storage::StorageConfig{datasetBase(config, profile), result.disable_feature_dataset};
    result.dataset_name = result.use_shared_memory ? datasetName(config, profile) : "";
    return result;
}
} // namespace osrm::deployment
