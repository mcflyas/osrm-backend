#ifndef OSRM_TEST_DEPLOYMENT_FIXTURE_HPP
#define OSRM_TEST_DEPLOYMENT_FIXTURE_HPP

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

struct DeploymentFixture
{
    std::filesystem::path Directory;

    DeploymentFixture()
    {
        static std::atomic<unsigned> sequence{0};
        Directory = std::filesystem::temp_directory_path() /
                    ("osrm-deployment-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(sequence++));
        std::filesystem::create_directories(Directory);
    }

    ~DeploymentFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(Directory, error);
    }

    std::filesystem::path write(const std::string &name, const std::string &contents)
    {
        const auto path = Directory / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << contents;
        return path;
    }

    std::filesystem::path config(const std::string &extra = "")
    {
        return write("configuration.json",
                     R"({"version":1,"output_directory":"data folder/ø",
                         "profiles":[{"name":"car","algorithm":"MLD"}])" +
                         extra + "}");
    }
};
#endif
