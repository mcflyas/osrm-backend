#include "fixture.hpp"

#include "engine/datafacade.hpp"
#include "engine/datafacade/mmap_memory_allocator.hpp"
#include "engine/search_engine_data.hpp"
#include "osrm/route_parameters.hpp"
#include "util/json_renderer.hpp"

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <vector>

namespace
{
using Algorithm = osrm::engine::routing_algorithms::mld::Algorithm;
using WorkingData = osrm::engine::SearchEngineData<Algorithm>;

osrm::engine::DataFacade<Algorithm> loadFacade(const std::string &path)
{
    return {std::make_shared<osrm::engine::datafacade::MMapMemoryAllocator>(
                osrm::storage::StorageConfig{path}),
            "duration",
            0};
}

osrm::RouteParameters query(unsigned column, unsigned row)
{
    osrm::RouteParameters parameters;
    parameters.coordinates = {
        {osrm::util::FloatLongitude{7.001 + column * .002}, osrm::util::FloatLatitude{43.0}},
        {osrm::util::FloatLongitude{7.029}, osrm::util::FloatLatitude{43.002 + row * .002}}};
    parameters.overview = osrm::RouteParameters::OverviewType::Full;
    parameters.annotations = true;
    return parameters;
}

std::string runRoute(osrm::OSRM &engine, const osrm::RouteParameters &parameters)
{
    osrm::json::Object result;
    BOOST_REQUIRE(engine.Route(parameters, result) == osrm::Status::Ok);
    std::string serialized;
    osrm::util::json::render(serialized, result);
    return serialized;
}

std::string standaloneRoute(osrm::OSRM &engine, const osrm::RouteParameters &parameters)
{
    WorkingData::unpacking_cache.reset();
    return runRoute(engine, parameters);
}
} // namespace

BOOST_AUTO_TEST_SUITE(multiple_datasets)

BOOST_AUTO_TEST_CASE(alternating_equal_sized_metrics)
{
    const std::string path_a = OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm";
    const std::string path_b = OSRM_TEST_DATA_DIR "/multiple_profiles/b/map.osrm";
    const auto facade_a = loadFacade(path_a);
    const auto facade_b = loadFacade(path_b);
    BOOST_REQUIRE_EQUAL(facade_a.GetNumberOfNodes(), facade_b.GetNumberOfNodes());
    BOOST_REQUIRE_EQUAL(facade_a.GetNumberOfEdges(), facade_b.GetNumberOfEdges());
    BOOST_REQUIRE_GT(facade_a.GetMultiLevelPartition().GetNumberOfCells(1), 1);

    auto engine_a = getOSRM(path_a, osrm::EngineConfig::Algorithm::MLD);
    auto engine_b = getOSRM(path_b, osrm::EngineConfig::Algorithm::MLD);
    for (unsigned column = 0; column < 12; column += 2)
    {
        for (unsigned row = 3; row < 14; row += 2)
        {
            const auto parameters = query(column, row);
            const auto expected_a = standaloneRoute(engine_a, parameters);
            const auto expected_b = standaloneRoute(engine_b, parameters);
            BOOST_REQUIRE(expected_a != expected_b);
            WorkingData::unpacking_cache.reset();
            BOOST_CHECK_EQUAL(runRoute(engine_a, parameters), expected_a);
            BOOST_CHECK_EQUAL(runRoute(engine_b, parameters), expected_b);
            BOOST_CHECK_EQUAL(runRoute(engine_a, parameters), expected_a);
        }
    }
}

BOOST_AUTO_TEST_CASE(alternating_exclusion_views)
{
    auto engine = getOSRM(OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm",
                          osrm::EngineConfig::Algorithm::MLD);
    auto parameters = query(2, 8);
    auto excluded = parameters;
    excluded.exclude = {"toll"};
    const auto expected = standaloneRoute(engine, parameters);
    const auto expected_excluded = standaloneRoute(engine, excluded);
    BOOST_REQUIRE(expected != expected_excluded);
    for (unsigned repetition = 0; repetition < 3; ++repetition)
    {
        BOOST_CHECK_EQUAL(runRoute(engine, parameters), expected);
        BOOST_CHECK_EQUAL(runRoute(engine, excluded), expected_excluded);
    }
}

BOOST_AUTO_TEST_CASE(alternating_graph_sizes)
{
    auto small = getOSRM(OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm",
                         osrm::EngineConfig::Algorithm::MLD);
    auto large = getOSRM(OSRM_TEST_DATA_DIR "/mld/monaco.osrm", osrm::EngineConfig::Algorithm::MLD);
    const auto small_query = query(2, 8);
    osrm::RouteParameters large_query;
    large_query.coordinates = {
        {osrm::util::FloatLongitude{7.415800}, osrm::util::FloatLatitude{43.734132}},
        {osrm::util::FloatLongitude{7.421315}, osrm::util::FloatLatitude{43.738814}}};
    const auto expected_small = standaloneRoute(small, small_query);
    const auto expected_large = standaloneRoute(large, large_query);
    for (unsigned repetition = 0; repetition < 3; ++repetition)
    {
        BOOST_CHECK_EQUAL(runRoute(small, small_query), expected_small);
        BOOST_CHECK_EQUAL(runRoute(large, large_query), expected_large);
    }
}

BOOST_AUTO_TEST_SUITE_END()
