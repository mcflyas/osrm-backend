#include "../helpers/shared_memory.hpp"
#include "fixture.hpp"

#include "storage/storage.hpp"
#include "engine/data_watchdog.hpp"
#include "engine/datafacade.hpp"
#include "engine/datafacade/mmap_memory_allocator.hpp"
#include "engine/plugins/viaroute.hpp"
#include "engine/routing_algorithms.hpp"
#include "engine/search_engine_data.hpp"
#include "osrm/route_parameters.hpp"
#include "util/json_renderer.hpp"

#include <boost/test/unit_test.hpp>

#include <future>
#include <memory>
#include <string>
#include <thread>
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

BOOST_FIXTURE_TEST_SUITE(shared_dataset_updates, SharedMemoryFixture)

BOOST_AUTO_TEST_CASE(retained_facade_is_coherent_across_metric_publication)
{
    const std::string path_a = OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm";
    const std::string path_b = OSRM_TEST_DATA_DIR "/multiple_profiles/b/map.osrm";
    const auto name = Namespace + "/car";
    BOOST_REQUIRE_EQUAL(osrm::storage::Storage({path_a}).Run(-1, name, false), 0);
    osrm::engine::DataWatchdog<Algorithm,
                               osrm::engine::datafacade::ContiguousInternalMemoryDataFacade>
        watcher(name);
    auto parameters = query(2, 8);
    auto old_view = watcher.Get(parameters);
    parameters.generate_hints = false;
    auto standalone_a = getOSRM(path_a, osrm::EngineConfig::Algorithm::MLD);
    auto standalone_b = getOSRM(path_b, osrm::EngineConfig::Algorithm::MLD);
    const auto expected_a = standaloneRoute(standalone_a, parameters);
    const auto expected_b = standaloneRoute(standalone_b, parameters);
    BOOST_REQUIRE(expected_a != expected_b);
    const auto route_view = [&](const auto &view)
    {
        WorkingData heaps;
        heaps.selectDataset(view);
        osrm::engine::RoutingAlgorithms<Algorithm> algorithms(heaps, view);
        osrm::engine::api::ResultT result;
        osrm::engine::plugins::ViaRoutePlugin plugin(500, 3, std::nullopt);
        BOOST_REQUIRE(plugin.HandleRequest(algorithms, parameters, result) == osrm::Status::Ok);
        std::string serialized;
        osrm::util::json::render(serialized, std::get<osrm::json::Object>(result));
        return serialized;
    };
    BOOST_CHECK_EQUAL(route_view(old_view), expected_a);
    auto publisher = std::async(
        std::launch::async, [&] { return osrm::storage::Storage({path_b}).Run(-1, name, true); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto new_view = watcher.Get(parameters);
    while (new_view == old_view && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        new_view = watcher.Get(parameters);
    }
    BOOST_REQUIRE(new_view != old_view);
    BOOST_CHECK_EQUAL(new_view->GetNumberOfNodes(), old_view->GetNumberOfNodes());
    BOOST_CHECK_EQUAL(new_view->GetNumberOfEdges(), old_view->GetNumberOfEdges());
    BOOST_CHECK_EQUAL(route_view(new_view), expected_b);
    BOOST_CHECK_EQUAL(route_view(old_view), expected_a);
    BOOST_CHECK_EQUAL(route_view(new_view), expected_b);
    old_view.reset();
    BOOST_CHECK_EQUAL(publisher.get(), 0);
}

BOOST_AUTO_TEST_CASE(engine_cache_changes_after_metric_publication)
{
    const std::string path_a = OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm";
    const std::string path_b = OSRM_TEST_DATA_DIR "/multiple_profiles/b/map.osrm";
    const auto name = Namespace + "/car";
    auto parameters = query(2, 8);
    parameters.generate_hints = false;
    auto standalone_a = getOSRM(path_a, osrm::EngineConfig::Algorithm::MLD);
    auto standalone_b = getOSRM(path_b, osrm::EngineConfig::Algorithm::MLD);
    const auto expected_a = standaloneRoute(standalone_a, parameters);
    const auto expected_b = standaloneRoute(standalone_b, parameters);
    BOOST_REQUIRE(expected_a != expected_b);
    BOOST_REQUIRE_EQUAL(osrm::storage::Storage({path_a}).Run(-1, name, false), 0);
    osrm::EngineConfig config;
    config.use_shared_memory = true;
    config.dataset_name = name;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;
    osrm::OSRM engine(config);
    BOOST_REQUIRE_EQUAL(runRoute(engine, parameters), expected_a);
    auto publisher = std::async(
        std::launch::async, [&] { return osrm::storage::Storage({path_b}).Run(-1, name, true); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto updated = runRoute(engine, parameters);
    while (updated == expected_a && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        updated = runRoute(engine, parameters);
    }
    BOOST_CHECK_EQUAL(updated, expected_b);
    BOOST_CHECK_EQUAL(publisher.get(), 0);
}

BOOST_AUTO_TEST_CASE(publication_timeout_returns_failure_and_releases_reserved_keys)
{
    using Monitor = osrm::storage::SharedMonitor<osrm::storage::SharedRegionRegister>;
    Monitor monitor(osrm::storage::SharedRegionRegister{});
    const auto available = monitor.data().ReserveKey();
    const auto second_available = monitor.data().ReserveKey();
    monitor.data().ReleaseKey(second_available);
    monitor.data().ReleaseKey(available);
    boost::interprocess::scoped_lock lock(monitor.get_mutex());
    const auto status = osrm::storage::Storage({OSRM_TEST_DATA_DIR "/multiple_profiles/a/map.osrm"})
                            .Run(0, Namespace + "/timeout", false);
    BOOST_CHECK_NE(status, 0);
    const auto next = monitor.data().ReserveKey();
    BOOST_CHECK_EQUAL(next, available);
    monitor.data().ReleaseKey(next);
    if (next != available)
    {
        monitor.data().ReleaseKey(available);
        monitor.data().ReleaseKey(second_available);
    }
}

BOOST_AUTO_TEST_SUITE_END()
