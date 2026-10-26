#include "deployment/config.hpp"

#include "fixture.hpp"
#include "storage/shared_datatype.hpp"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(deployment_config, DeploymentFixture)

BOOST_AUTO_TEST_CASE(relative_paths_and_serving_defaults)
{
    std::string error;
    const auto result = osrm::deployment::readConfig(config(), error);
    BOOST_REQUIRE_MESSAGE(result, error);
    BOOST_CHECK(result->OutputDirectory == Directory / "data folder/ø");
    BOOST_CHECK(result->Server.Storage == osrm::deployment::StorageMode::MMap);
    BOOST_CHECK_EQUAL(result->Server.Port, 5000);
    BOOST_CHECK_EQUAL(result->Server.EngineDefaults.max_locations_viaroute, 500);
    BOOST_CHECK_EQUAL(result->Server.EngineDefaults.max_locations_distance_table, 100);
    BOOST_CHECK_GT(result->PreparationThreads, 0);
    BOOST_CHECK(osrm::deployment::datasetBase(*result, result->Profiles.front()) ==
                Directory / "data folder/ø/car/map.osrm");
    BOOST_CHECK(
        osrm::deployment::validateConfig(*result, osrm::deployment::ConfigPurpose::Serve, error));
    BOOST_CHECK(!osrm::deployment::validateConfig(
        *result, osrm::deployment::ConfigPurpose::Prepare, error));
    BOOST_CHECK(!std::filesystem::exists(result->OutputDirectory));
}

BOOST_AUTO_TEST_CASE(preparation_requires_existing_inputs)
{
    std::string error;
    auto result = osrm::deployment::readConfig(config(), error);
    BOOST_REQUIRE(result);
    result->Input = write("map.osm", "map");
    result->Profiles.front().Lua = Directory / "missing.lua";
    BOOST_CHECK(!osrm::deployment::validateConfig(
        *result, osrm::deployment::ConfigPurpose::Prepare, error));
    result->Profiles.front().Lua = write("car.lua", "profile");
    BOOST_CHECK(
        osrm::deployment::validateConfig(*result, osrm::deployment::ConfigPurpose::Prepare, error));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_schema_values)
{
    const std::vector<std::string> invalid{R"(,"version":1)",
                                           R"(,"unknown":true)",
                                           R"(,"preparation":{"threads":0})",
                                           R"(,"preparation":{"threads":-1})",
                                           R"(,"preparation":{"threads":2,"threads":3})",
                                           R"(,"server":{"threads":4294967296})",
                                           R"(,"server":{"port":65536})",
                                           R"(,"server":{"port":-1})",
                                           R"(,"server":{"max_table_size":2})",
                                           R"(,"server":{"max_alternatives":-1})",
                                           R"(,"server":{"default_radius":0})",
                                           R"(,"server":{"default_radius":"infinity"})",
                                           R"(,"server":{"storage":"unknown"})",
                                           R"(,"server":{"storage":"shared_memory"})",
                                           R"(,"server":{"port":5000,"port":5001})",
                                           R"(,"server":{"unrecognized":1})",
                                           R"(,"input":"map\u0000.osm")",
                                           R"(,"input":42)",
                                           R"(,"server":{"disable_feature_dataset":["unknown"]})",
                                           R"(,"server":{"verbosity":"nonsense"})"};
    for (const auto &extra : invalid)
    {
        std::string error;
        BOOST_CHECK_MESSAGE(!osrm::deployment::readConfig(config(extra), error), extra);
        BOOST_CHECK_MESSAGE(!error.empty(), extra);
    }
}

BOOST_AUTO_TEST_CASE(rejects_invalid_profile_lists)
{
    for (const std::string profiles :
         {R"([])",
          R"([{"name":"../car","algorithm":"MLD"}])",
          R"([{"name":"","algorithm":"MLD"}])",
          R"([{"name":"car","algorithm":"other"}])",
          R"([{"name":"car","name":"bus","algorithm":"CH"}])",
          R"([{"name":"car","algorithm":"CH","unknown":true}])",
          R"([{"name":"car\u0000bus","algorithm":"CH"}])",
          R"([{"name":"foot path","algorithm":"CH"}])",
          R"([{"name":"car"}])",
          R"([{"name":"car","algorithm":"MLD","lua":""}])",
          R"([{"name":"car","algorithm":"CH"},{"name":"car","algorithm":"MLD"}])"})
    {
        std::string error;
        const auto path =
            write("bad.json",
                  "{\"version\":1,\"output_directory\":\"out\",\"profiles\":" + profiles + "}");
        BOOST_CHECK_MESSAGE(!osrm::deployment::readConfig(path, error), profiles);
    }
    std::string error;
    BOOST_CHECK(!osrm::deployment::readConfig(write("bad.json", "{bad json}"), error));
    BOOST_CHECK(!osrm::deployment::readConfig(write("bad.json", R"({"version":2})"), error));
}

BOOST_AUTO_TEST_CASE(shared_memory_names_and_engine_configuration)
{
    std::string error;
    auto result = osrm::deployment::readConfig(
        config(
            R"(,"server":{"storage":"shared_memory","dataset_namespace":"my-service","max_table_size":123,"default_radius":"unlimited"})"),
        error);
    BOOST_REQUIRE_MESSAGE(result, error);
    BOOST_CHECK_EQUAL(osrm::deployment::datasetName(*result, result->Profiles[0]),
                      "my-service/car");
    auto engine = osrm::deployment::makeEngineConfig(*result, result->Profiles[0]);
    BOOST_CHECK(engine.use_shared_memory);
    BOOST_CHECK(engine.storage_config.GetPath("").empty());
    BOOST_CHECK_EQUAL(engine.max_locations_distance_table, 123);
    BOOST_CHECK_EQUAL(engine.default_radius, -1);
    BOOST_CHECK(
        osrm::deployment::validateConfig(*result, osrm::deployment::ConfigPurpose::Store, error));
    result->Server.DatasetNamespace =
        std::string(osrm::storage::SharedRegion::MAX_NAME_LENGTH, 'x');
    BOOST_CHECK(
        !osrm::deployment::validateConfig(*result, osrm::deployment::ConfigPurpose::Serve, error));
    result->Server.Storage = osrm::deployment::StorageMode::Memory;
    engine = osrm::deployment::makeEngineConfig(*result, result->Profiles[0]);
    BOOST_CHECK(!engine.use_shared_memory);
    BOOST_CHECK(!engine.use_mmap);
}

BOOST_AUTO_TEST_SUITE_END()
