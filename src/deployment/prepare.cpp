#include "deployment/prepare.hpp"

#include "contractor/contractor.hpp"
#include "customizer/customizer.hpp"
#include "deployment/descriptor.hpp"
#include "deployment/output_directory.hpp"
#include "extractor/driving_side_index.hpp"
#include "extractor/extractor.hpp"
#include "extractor/scripting_environment_lua.hpp"
#include "partitioner/partitioner.hpp"
#include "util/log.hpp"

namespace osrm::deployment
{
int prepare(const DeploymentConfig &config, std::string &error)
{
    if (!validateConfig(config, ConfigPurpose::Prepare, error))
        return 1;
    auto output = OutputDirectory::acquire(config.OutputDirectory, error);
    if (!output)
        return 1;
    std::string active_profile;
    std::string stage = "preflight";
    try
    {
        for (const auto &profile : config.Profiles)
        {
            active_profile = profile.Name;
            if (!std::filesystem::create_directory(datasetBase(config, profile).parent_path()))
            {
                error = "Profile " + active_profile +
                        " preflight: output directory already exists (filesystem alias)";
                return 1;
            }
        }
        for (const auto &profile : config.Profiles)
        {
            active_profile = profile.Name;
            const auto base = datasetBase(config, profile);
            stage = "extract";
            util::Log() << "Profile " << active_profile << ": " << stage;
            extractor::ExtractorConfig extraction;
            extraction.input_path = config.Input;
            extraction.profile_path = profile.Lua;
            extraction.output_path = base;
            extraction.UseDefaultOutputNames(base);
            extraction.requested_num_threads = config.PreparationThreads;
            const auto driving_side_mode =
                extractor::DrivingSideIndex::ParseMode(extraction.driving_side_index)
                    .value_or(extractor::DrivingSideIndex::Mode::Off);
            extractor::Sol2ScriptingEnvironment scripting_environment(
                extraction.profile_path.string(),
                extraction.location_dependent_data_paths,
                driving_side_mode);
            int status = extractor::Extractor(extraction).run(scripting_environment);
            if (status == 0 && profile.Algorithm == engine::EngineConfig::Algorithm::CH)
            {
                stage = "contract";
                util::Log() << "Profile " << active_profile << ": " << stage;
                contractor::ContractorConfig contraction;
                contraction.UseDefaultOutputNames(base);
                contraction.requested_num_threads = config.PreparationThreads;
                status = contractor::Contractor(contraction).Run();
            }
            else if (status == 0)
            {
                stage = "partition";
                util::Log() << "Profile " << active_profile << ": " << stage;
                partitioner::PartitionerConfig partition;
                partition.UseDefaultOutputNames(base);
                partition.requested_num_threads = config.PreparationThreads;
                status = partitioner::Partitioner().Run(partition);
                if (status == 0)
                {
                    stage = "customize";
                    util::Log() << "Profile " << active_profile << ": " << stage;
                    customizer::CustomizationConfig customization;
                    customization.UseDefaultOutputNames(base);
                    customization.requested_num_threads = config.PreparationThreads;
                    status = customizer::Customizer().Run(customization);
                }
            }
            if (status != 0)
            {
                error = "Profile " + active_profile + " " + stage + " failed with status " +
                        std::to_string(status);
                return status;
            }
        }
        stage = "completion";
        return writeCompletionDescriptor(config, error) ? 0 : 1;
    }
    catch (const std::exception &exception)
    {
        error = "Profile " + active_profile + " " + stage + ": " + exception.what();
        return 1;
    }
}
} // namespace osrm::deployment
