#include "deployment/prepare.hpp"
#include "deployment/config.hpp"
#include "util/log.hpp"
#include "util/version.hpp"

#include <boost/program_options.hpp>

#include <iostream>
#include <limits>

int main(int argc, char *argv[])
{
    namespace options = boost::program_options;
    options::options_description description("osrm-prepare --config <path> [options]");
    description.add_options()("help,h", "Show this help message")("version,v", "Show version")(
        "config", options::value<std::string>(), "Deployment configuration file")(
        "threads,t", options::value<long long>(), "Preparation thread budget")(
        "verbosity,l", options::value<std::string>(), "Log verbosity level");
    try
    {
        options::variables_map variables;
        options::store(options::parse_command_line(argc, argv, description), variables);
        if (variables.contains("help"))
        {
            std::cout << description;
            return 0;
        }
        if (variables.contains("version"))
        {
            std::cout << OSRM_VERSION << '\n';
            return 0;
        }
        options::notify(variables);
        if (!variables.contains("config"))
        {
            std::cerr << description;
            return 1;
        }
        std::string error;
        auto config = osrm::deployment::readConfig(variables["config"].as<std::string>(), error);
        if (!config)
        {
            std::cerr << error << '\n';
            return 1;
        }
        if (variables.contains("threads"))
        {
            const auto threads = variables["threads"].as<long long>();
            if (threads <= 0 || threads > std::numeric_limits<unsigned>::max())
            {
                std::cerr << "threads must be a positive unsigned integer\n";
                return 1;
            }
            config->PreparationThreads = static_cast<unsigned>(threads);
        }
        if (variables.contains("verbosity"))
            config->Server.EngineDefaults.verbosity = variables["verbosity"].as<std::string>();
        if (!osrm::deployment::validateConfig(
                *config, osrm::deployment::ConfigPurpose::Prepare, error))
        {
            std::cerr << error << '\n';
            return 1;
        }
        osrm::util::LogPolicy::GetInstance().SetLevel(config->Server.EngineDefaults.verbosity);
        const auto status = osrm::deployment::prepare(*config, error);
        if (status != 0)
            std::cerr << error << '\n';
        return status;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
