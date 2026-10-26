#include "deployment/store.hpp"

#include "deployment/descriptor.hpp"
#include "storage/storage.hpp"
#include "util/log.hpp"

namespace osrm::deployment
{
int publishProfiles(const DeploymentConfig &config, int max_wait, std::string &error)
{
    if (!validateConfig(config, ConfigPurpose::Store, error) ||
        !validatePreparedData(config, error))
        return 1;
    for (const auto &profile : config.Profiles)
    {
        try
        {
            storage::Storage storage(
                storage::StorageConfig(datasetBase(config, profile),
                                       config.Server.EngineDefaults.disable_feature_dataset),
                true);
            const auto status = storage.Run(max_wait, datasetName(config, profile), false);
            if (status != 0)
            {
                error = "Profile " + profile.Name + ": publication failed with status " +
                        std::to_string(status);
                return status;
            }
            util::Log() << "Published profile " << profile.Name << " as "
                        << datasetName(config, profile);
        }
        catch (const std::exception &exception)
        {
            error = "Profile " + profile.Name + ": publication failed: " + exception.what();
            return 1;
        }
    }
    return 0;
}
} // namespace osrm::deployment
