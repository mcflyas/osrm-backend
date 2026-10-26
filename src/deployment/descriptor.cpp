#include "deployment/descriptor.hpp"

#include "json.hpp"
#include "storage/storage_config.hpp"

#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

#include <fstream>

namespace osrm::deployment
{
namespace
{
const char *algorithmName(const ProfileConfig &profile)
{ return profile.Algorithm == engine::EngineConfig::Algorithm::CH ? "CH" : "MLD"; }

bool validateArtifacts(const DeploymentConfig &config, std::string &error)
{
    for (const auto &profile : config.Profiles)
    {
        const storage::StorageConfig storage(datasetBase(config, profile),
                                             config.Server.EngineDefaults.disable_feature_dataset);
        auto required =
            storage::GetRequiredFiles(config.Server.EngineDefaults.disable_feature_dataset);
        if (profile.Algorithm == engine::EngineConfig::Algorithm::CH)
            required.emplace_back(".osrm.hsgr");
        else
            for (const auto *extension :
                 {".osrm.partition", ".osrm.cells", ".osrm.cell_metrics", ".osrm.mldgr"})
                required.emplace_back(extension);
        for (const auto &extension : required)
        {
            std::error_code code;
            const auto path = storage.GetPath(extension.string());
            if (!std::filesystem::is_regular_file(path, code))
                return detail::fail(
                    error, "Profile " + profile.Name + ": missing artifact " + path.string());
        }
    }
    return true;
}
} // namespace

bool writeCompletionDescriptor(const DeploymentConfig &config, std::string &error)
{
    error.clear();
    if (!validateArtifacts(config, error))
        return false;
    const auto destination = config.OutputDirectory / "prepared.json";
    const auto temporary = config.OutputDirectory / "prepared.json.tmp";
    std::error_code code;
    if (std::filesystem::exists(destination, code) || code)
        return detail::fail(error,
                            "Completion descriptor already exists or is inaccessible: " +
                                destination.string());
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return detail::fail(error, "Cannot write " + temporary.string());
        rapidjson::OStreamWrapper wrapper(stream);
        rapidjson::Writer<rapidjson::OStreamWrapper> writer(wrapper);
        writer.StartObject();
        writer.Key("version");
        writer.Int(1);
        writer.Key("profiles");
        writer.StartArray();
        for (const auto &profile : config.Profiles)
        {
            writer.StartObject();
            writer.Key("name");
            writer.String(profile.Name.c_str());
            writer.Key("algorithm");
            writer.String(algorithmName(profile));
            writer.Key("dataset");
            writer.String((profile.Name + "/map.osrm").c_str());
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        stream.flush();
        if (!stream)
            return detail::fail(error, "Cannot write " + temporary.string());
        stream.close();
        if (stream.fail())
            return detail::fail(error, "Cannot close " + temporary.string());
    }
    std::filesystem::rename(temporary, destination, code);
    if (code)
        return detail::fail(error,
                            "Cannot publish " + destination.string() + ": " + code.message());
    return true;
}

bool validatePreparedData(const DeploymentConfig &config, std::string &error)
{
    auto json = detail::readJson(config.OutputDirectory / "prepared.json", error);
    if (!json || !detail::members(*json, {"version", "profiles"}, "prepared", error))
        return false;
    const auto &value = *json;
    if (!value.HasMember("version") || !value["version"].IsInt() ||
        value["version"].GetInt() != 1 || !value.HasMember("profiles") ||
        !value["profiles"].IsArray() || value["profiles"].Size() != config.Profiles.size())
        return detail::fail(error,
                            "prepared.json: version or profile list does not match configuration");
    for (rapidjson::SizeType i = 0; i < value["profiles"].Size(); ++i)
    {
        const auto &entry = value["profiles"][i];
        const auto &profile = config.Profiles[i];
        std::string name, algorithm, dataset;
        if (!detail::members(entry, {"name", "algorithm", "dataset"}, "prepared.profiles", error))
            return false;
        if (!entry.HasMember("name") || !entry.HasMember("algorithm") ||
            !entry.HasMember("dataset"))
            return detail::fail(error, "prepared.json: incomplete profile " + profile.Name);
        if (!detail::string(entry["name"], name, "prepared.name", error) ||
            !detail::string(entry["algorithm"], algorithm, "prepared.algorithm", error) ||
            !detail::string(entry["dataset"], dataset, "prepared.dataset", error))
            return false;
        if (name != profile.Name || algorithm != algorithmName(profile) ||
            dataset != profile.Name + "/map.osrm")
            return detail::fail(
                error, "prepared.json: profile does not match configuration: " + profile.Name);
    }
    return validateArtifacts(config, error);
}
} // namespace osrm::deployment
