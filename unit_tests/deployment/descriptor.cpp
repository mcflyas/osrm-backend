#include "deployment/descriptor.hpp"

#include "fixture.hpp"
#include "storage/storage_config.hpp"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(deployment_descriptor, DeploymentFixture)

BOOST_AUTO_TEST_CASE(completion_requires_artifacts_and_matches_config)
{
    std::string error;
    auto value = osrm::deployment::readConfig(config(), error);
    BOOST_REQUIRE(value);
    BOOST_CHECK(!osrm::deployment::writeCompletionDescriptor(*value, error));
    BOOST_CHECK(!osrm::deployment::validatePreparedData(*value, error));
    for (const auto &extension : osrm::storage::GetRequiredFiles({}))
        write("data folder/ø/car/map" + extension.string(), "");
    for (const auto *extension :
         {".osrm.partition", ".osrm.cells", ".osrm.cell_metrics", ".osrm.mldgr"})
        write(std::string("data folder/ø/car/map") + extension, "");
    BOOST_REQUIRE_MESSAGE(osrm::deployment::writeCompletionDescriptor(*value, error), error);
    BOOST_CHECK_MESSAGE(osrm::deployment::validatePreparedData(*value, error), error);
    BOOST_CHECK(!osrm::deployment::writeCompletionDescriptor(*value, error));
    value->Profiles[0].Algorithm = osrm::engine::EngineConfig::Algorithm::CH;
    BOOST_CHECK(!osrm::deployment::validatePreparedData(*value, error));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_descriptors)
{
    std::string error;
    const auto value = osrm::deployment::readConfig(config(), error);
    BOOST_REQUIRE(value);
    for (
        const auto *contents :
        {"{",
         R"({"version":2,"profiles":[]})",
         R"({"version":1,"profiles":[]})",
         R"({"version":1,"profiles":[{"name":"foot","algorithm":"MLD","dataset":"car/map.osrm"}]})",
         R"({"version":1,"profiles":[{"name":"car","algorithm":"MLD","dataset":"../map.osrm"}]})",
         R"({"version":1,"profiles":[{"name":"car","name":"car","algorithm":"MLD","dataset":"car/map.osrm"}]})"})
    {
        write("data folder/ø/prepared.json", contents);
        BOOST_CHECK_MESSAGE(!osrm::deployment::validatePreparedData(*value, error), contents);
        BOOST_CHECK(!error.empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()
